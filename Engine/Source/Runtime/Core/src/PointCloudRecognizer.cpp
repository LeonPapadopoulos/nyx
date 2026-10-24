#include "NyxPCH.h"
#include "PointCloudRecognizer.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace Nyx
{
	namespace
	{
		// A turn by its cosine and sine. Turns are made only from the constants below by
		// multiplying, never with a C library's cos and sin, whose last bits can differ between
		// machines.
		struct Turn
		{
			float Cos = 1.0f;
			float Sin = 0.0f;
		};

		// Both turns, one after the other
		Turn Compose(Turn a, Turn b)
		{
			return Turn{ a.Cos * b.Cos - a.Sin * b.Sin, a.Sin * b.Cos + a.Cos * b.Sin };
		}

		Turn Inverse(Turn turn)
		{
			return Turn{ turn.Cos, -turn.Sin };
		}

		// Counterclockwise, +Y up
		glm::vec2 Apply(Turn turn, glm::vec2 point)
		{
			return glm::vec2(turn.Cos * point.x - turn.Sin * point.y, turn.Sin * point.x + turn.Cos * point.y);
		}

		constexpr float Cos22_5 = 0.923879532511286756f;
		constexpr float Sin22_5 = 0.382683432365089772f;
		constexpr float Cos45 = 0.707106781186547524f;
		constexpr float Cos11_25 = 0.980785280403230449f;
		constexpr float Sin11_25 = 0.195090322016128268f;
		constexpr float Cos5_625 = 0.995184726672196886f;
		constexpr float Sin5_625 = 0.0980171403295606020f;

		// Rotation insensitive: every template is tried at 8 turns 45 degrees apart, then at half,
		// a quarter and an eighth of that either side of the best of them
		constexpr size_t CoarseTurnCount = 8;

		std::vector<Turn> MakeCoarseTurns()
		{
			// The first quarter, then each next quarter is the last a quarter turn on: (c, s) to (-s, c), exactly
			const Turn quarter[2] = { { 1.0f, 0.0f }, { Cos45, Cos45 } };
			std::vector<Turn> turns;
			for (size_t index = 0; index < CoarseTurnCount; ++index)
			{
				Turn turn = quarter[index % 2];
				for (size_t quarters = 0; quarters < index / 2; ++quarters)
				{
					turn = Turn{ -turn.Sin, turn.Cos };
				}
				turns.push_back(turn);
			}
			return turns;
		}

		float GetPathLength(const std::vector<Stroke>& strokes)
		{
			float length = 0.0f;
			for (const Stroke& stroke : strokes)
			{
				for (size_t index = 1; index < stroke.size(); ++index)
				{
					length += glm::distance(stroke[index - 1], stroke[index]);
				}
			}
			return length;
		}

		// pointCount points evenly along the strokes, as $P's Resample: the first point of the
		// first stroke, then one each pathLength / (pointCount - 1) along them, the gaps between
		// strokes left out; the distance since the last point goes on into the next stroke
		std::vector<glm::vec2> Resample(const std::vector<Stroke>& strokes, uint32_t pointCount)
		{
			const float pathLength = GetPathLength(strokes);
			if (pointCount < 2 || !(pathLength > 0.0f) || !std::isfinite(pathLength))
			{
				return {};
			}

			const float interval = pathLength / static_cast<float>(pointCount - 1);
			std::vector<glm::vec2> points;
			points.reserve(pointCount);
			float sinceLast = 0.0f;
			glm::vec2 last{ 0.0f };
			for (const Stroke& stroke : strokes)
			{
				if (stroke.empty())
				{
					continue;
				}
				if (points.empty())
				{
					points.push_back(stroke.front());
				}

				glm::vec2 previous = stroke.front();
				for (size_t index = 1; index < stroke.size() && points.size() < pointCount; ++index)
				{
					const glm::vec2 point = stroke[index];
					float segment = glm::distance(previous, point);
					while (segment > 0.0f && sinceLast + segment >= interval && points.size() < pointCount)
					{
						const glm::vec2 between = previous + ((interval - sinceLast) / segment) * (point - previous);
						points.push_back(between);
						previous = between;
						segment = glm::distance(previous, point);
						sinceLast = 0.0f;
					}
					sinceLast += segment;
					previous = point;
				}
				last = stroke.back();
			}

			// Rounding can leave the last point out
			while (points.size() < pointCount)
			{
				points.push_back(last);
			}
			return points;
		}

		glm::vec2 GetCentroid(const std::vector<glm::vec2>& points)
		{
			glm::vec2 sum{ 0.0f };
			for (const glm::vec2& point : points)
			{
				sum += point;
			}
			return sum / static_cast<float>(points.size());
		}

		// Points round the origin, turned, then scaled so that their box's longer side is 1. Turning
		// and scaling about the origin keep the centroid there. False for points all in one place.
		bool TurnAndScale(const std::vector<glm::vec2>& centred, Turn turn, std::vector<glm::vec2>& outPoints)
		{
			outPoints.resize(centred.size());
			glm::vec2 lowest{ std::numeric_limits<float>::max() };
			glm::vec2 highest{ std::numeric_limits<float>::lowest() };
			for (size_t index = 0; index < centred.size(); ++index)
			{
				outPoints[index] = Apply(turn, centred[index]);
				lowest = glm::min(lowest, outPoints[index]);
				highest = glm::max(highest, outPoints[index]);
			}
			const float size = std::max(highest.x - lowest.x, highest.y - lowest.y);
			if (!(size > 0.0f))
			{
				return false;
			}
			const float scale = 1.0f / size;
			for (glm::vec2& point : outPoints)
			{
				point *= scale;
			}
			return true;
		}

		// Resampled and moved to their centroid, not yet scaled
		std::vector<glm::vec2> MakeCentredCloud(const std::vector<Stroke>& strokes, uint32_t pointCount)
		{
			std::vector<glm::vec2> points = Resample(strokes, pointCount);
			if (points.empty())
			{
				return {};
			}
			const glm::vec2 centroid = GetCentroid(points);
			for (glm::vec2& point : points)
			{
				point -= centroid;
			}
			return points;
		}

		// One cloud against one template's: every squared distance, both ways round, and what no
		// match can beat
		struct Pairing
		{
			// Row by row: the strokes' points' squared distances to the template's points
			std::vector<float> FromStrokes;
			// And the template's points' to the strokes'
			std::vector<float> FromTemplate;
			// Each point's distance to the nearest of the other cloud: the strokes', the template's
			std::vector<float> StrokesNearest;
			std::vector<float> TemplateNearest;
			// No match is less: the least lower bound over the starting points and both directions
			float Bound = 0.0f;
		};

		// $P's greedy cloud match between two clouds of the same size, with what one Recognize()
		// reuses for every template
		class CloudMatcher
		{
		public:
			explicit CloudMatcher(size_t pointCount)
				: Count(pointCount)
				, Taken(pointCount)
				, Weights(pointCount)
				, Remaining(pointCount + 1)
			{
				// $P's: the first point matched counts fully, the last 1/n
				for (size_t step = 0; step < Count; ++step)
				{
					Weights[step] = 1.0f - static_cast<float>(step) / static_cast<float>(Count);
				}

				// $P tries sqrt(n) starting points (its epsilon of 0.5), every one in both directions
				StartStep = 1;
				while ((StartStep + 1) * (StartStep + 1) <= Count)
				{
					++StartStep;
				}
			}

			// The sum of the weights: a match's sum over it is the weighted mean distance
			float GetWeightSum() const
			{
				float sum = 0.0f;
				for (const float weight : Weights)
				{
					sum += weight;
				}
				return sum;
			}

			// The distances between the clouds, x and y apart, and the bound
			void Prepare(const std::vector<float>& xs, const std::vector<float>& ys, const std::vector<float>& templateXs,
				const std::vector<float>& templateYs, Pairing& outPairing) const
			{
				GetSquaredDistances(xs.data(), ys.data(), templateXs.data(), templateYs.data(), outPairing.FromStrokes, outPairing.TemplateNearest);
				GetSquaredDistances(templateXs.data(), templateYs.data(), xs.data(), ys.data(), outPairing.FromTemplate, outPairing.StrokesNearest);
				outPairing.Bound = std::min(GetLeastLowerBound(outPairing.StrokesNearest), GetLeastLowerBound(outPairing.TemplateNearest));
			}

			// The least weighted sum of distances over the starting points and both directions, as
			// $P's GreedyCloudMatch; limit or more if it can't be under limit (it gives up early then)
			float Match(const Pairing& pairing, float limit)
			{
				float best = limit;
				for (size_t start = 0; start < Count && pairing.Bound < best; start += StartStep)
				{
					best = std::min(best, GetCloudDistance(pairing.FromStrokes, pairing.StrokesNearest, start, best));
					best = std::min(best, GetCloudDistance(pairing.FromTemplate, pairing.TemplateNearest, start, best));
				}
				return best;
			}

		private:
			// Row by row, each row's point's squared distance to every column's point, and each column's
			// point's distance to its nearest row: one pass the compiler runs several columns at a time
			// (counts and pointers in locals, so it sees that nothing written changes them)
			void GetSquaredDistances(const float* rowXs, const float* rowYs, const float* columnXs, const float* columnYs,
				std::vector<float>& outSquared, std::vector<float>& outColumnNearest) const
			{
				const size_t count = Count;
				outSquared.resize(count * count);
				outColumnNearest.assign(count, std::numeric_limits<float>::max());
				float* const nearest = outColumnNearest.data();
				for (size_t row = 0; row < count; ++row)
				{
					const float x = rowXs[row];
					const float y = rowYs[row];
					float* const squared = outSquared.data() + row * count;
					for (size_t column = 0; column < count; ++column)
					{
						const float dx = x - columnXs[column];
						const float dy = y - columnYs[column];
						const float distance = dx * dx + dy * dy;
						squared[column] = distance;
						nearest[column] = distance < nearest[column] ? distance : nearest[column];
					}
				}
				for (size_t column = 0; column < count; ++column)
				{
					nearest[column] = std::sqrt(nearest[column]);
				}
			}

			// No cloud distance from any starting point is less than this: each point's distance is at
			// least its nearest. With weights 1 - k/n the bound from each start follows from the last
			// one's (a sum S of the nearest, and T of the nearest by their step), and is made a little
			// smaller, so that rounding can never make it more than an exact sum.
			float GetLeastLowerBound(const std::vector<float>& nearest) const
			{
				const size_t count = Count;
				const float n = static_cast<float>(count);
				float sum = 0.0f;
				float bySteps = 0.0f;
				for (size_t index = 0; index < count; ++index)
				{
					sum += nearest[index];
					bySteps += static_cast<float>(index) * nearest[index];
				}
				float least = std::numeric_limits<float>::max();
				for (size_t start = 0, nextTried = 0; start < count; ++start)
				{
					if (start == nextTried)
					{
						least = std::min(least, sum - bySteps / n);
						nextTried += StartStep;
					}
					bySteps += n * nearest[start] - sum;
				}
				return least * 0.999f;
			}

			// $P's CloudDistance: from start on, each point of one cloud takes the nearest point of the
			// other not taken yet. Stops once the sum, with the least the points left can add, reaches
			// limit.
			float GetCloudDistance(const std::vector<float>& squaredDistances, const std::vector<float>& nearest, size_t start, float limit)
			{
				// From start to the end, then from the beginning: no remainder taken for every step
				const size_t count = Count;
				const size_t wrap = count - start;
				Remaining[count] = 0.0f;
				for (size_t step = count; step-- > 0;)
				{
					Remaining[step] = Remaining[step + 1] + Weights[step] * nearest[step < wrap ? start + step : step - wrap];
				}
				// A little less, as for the bounds: only a sum that can't be under limit stops early
				if (Remaining[0] * 0.999f >= limit)
				{
					return limit;
				}

				// Points taken are kept out by an infinite distance added to theirs
				std::fill(Taken.begin(), Taken.end(), 0.0f);
				float* const taken = Taken.data();
				float sum = 0.0f;
				for (size_t step = 0; step < count; ++step)
				{
					const float* const row = squaredDistances.data() + (step < wrap ? start + step : step - wrap) * count;
					size_t nearestColumn = 0;
					float nearestSquared = std::numeric_limits<float>::infinity();
					for (size_t column = 0; column < count; ++column)
					{
						const float squared = row[column] + taken[column];
						nearestColumn = squared < nearestSquared ? column : nearestColumn;
						nearestSquared = squared < nearestSquared ? squared : nearestSquared;
					}
					taken[nearestColumn] = std::numeric_limits<float>::infinity();
					sum += Weights[step] * std::sqrt(nearestSquared);
					if (sum + Remaining[step + 1] * 0.999f >= limit)
					{
						return limit;
					}
				}
				return sum;
			}

		private:
			size_t Count = 0;
			size_t StartStep = 1;
			// 0 for each point not taken yet in the match being made, infinity once taken
			std::vector<float> Taken;
			std::vector<float> Weights;
			// The least the steps from each on can add, for the match being made
			std::vector<float> Remaining;
		};

		// The best match so far, and the best of another name, in weighted sums
		struct Standings
		{
			struct Entry
			{
				size_t Template = 0;
				float Sum = 0.0f;
				Turn TurnToTemplate;
			};

			// What a template of this name has to beat to change the result
			float GetLimit(const std::string& name, const std::vector<std::string_view>& names) const
			{
				if (!Best)
				{
					return std::numeric_limits<float>::max();
				}
				if (name == names[Best->Template])
				{
					return Best->Sum;
				}
				return RunnerUp ? RunnerUp->Sum : std::numeric_limits<float>::max();
			}

			void Offer(const Entry& entry, const std::vector<std::string_view>& names)
			{
				if (!Best)
				{
					Best = entry;
					return;
				}
				if (names[entry.Template] == names[Best->Template])
				{
					if (entry.Sum < Best->Sum)
					{
						Best = entry;
					}
					return;
				}
				if (entry.Sum < Best->Sum)
				{
					RunnerUp = Best;
					Best = entry;
					return;
				}
				if (!RunnerUp || entry.Sum < RunnerUp->Sum)
				{
					RunnerUp = entry;
				}
			}

			std::optional<Entry> Best;
			std::optional<Entry> RunnerUp;
		};

		std::string_view Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(" \t\r");
			if (first == std::string_view::npos)
			{
				return {};
			}
			const size_t last = text.find_last_not_of(" \t\r");
			return text.substr(first, last - first + 1);
		}

		// The next word of text, taken off its front
		std::string_view TakeWord(std::string_view& text)
		{
			text = Trim(text);
			const size_t end = text.find_first_of(" \t");
			const std::string_view word = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end);
			return word;
		}

		void AppendNumber(std::string& text, float value)
		{
			char buffer[32];
			const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			text.append(buffer, result.ptr);
		}
	}

	PointCloudRecognizer::PointCloudRecognizer(PointCloudOptions options)
		: Options(options)
	{
	}

	bool PointCloudRecognizer::AddTemplate(std::string name, const std::vector<Stroke>& strokes)
	{
		const std::vector<glm::vec2> points = MakePointCloud(strokes, Options.PointCount);
		if (points.empty())
		{
			return false;
		}
		Template entry{ std::move(name), {}, {} };
		for (const glm::vec2& point : points)
		{
			entry.Xs.push_back(point.x);
			entry.Ys.push_back(point.y);
		}
		Templates.push_back(std::move(entry));
		return true;
	}

	PointCloudResult PointCloudRecognizer::Recognize(const std::vector<Stroke>& strokes, glm::vec2 up) const
	{
		const std::vector<glm::vec2> centred = MakeCentredCloud(strokes, Options.PointCount);
		if (centred.empty() || Templates.empty())
		{
			return {};
		}

		std::vector<std::string_view> names;
		names.reserve(Templates.size());
		for (const Template& entry : Templates)
		{
			names.push_back(entry.Name);
		}

		CloudMatcher matcher(centred.size());
		Standings standings;
		std::vector<glm::vec2> turned;

		// The strokes turned and scaled, x and y apart; false for strokes in one place
		const auto prepareTurn = [&](Turn turn, std::vector<float>& outXs, std::vector<float>& outYs)
		{
			if (!TurnAndScale(centred, turn, turned))
			{
				return false;
			}
			outXs.resize(turned.size());
			outYs.resize(turned.size());
			for (size_t index = 0; index < turned.size(); ++index)
			{
				outXs[index] = turned[index].x;
				outYs[index] = turned[index].y;
			}
			return true;
		};

		if (Options.bRotationSensitive)
		{
			// The turn that takes up to +Y: (x, y) to (up.y x - up.x y, up.x x + up.y y)
			const float length = glm::length(up);
			const glm::vec2 direction = length > 0.0f && std::isfinite(length) ? up / length : glm::vec2(0.0f, 1.0f);
			const Turn toTemplate{ direction.y, direction.x };
			std::vector<float> xs;
			std::vector<float> ys;
			if (!prepareTurn(toTemplate, xs, ys))
			{
				return {};
			}
			Pairing pairing;
			for (size_t index = 0; index < Templates.size(); ++index)
			{
				const float limit = standings.GetLimit(Templates[index].Name, names);
				matcher.Prepare(xs, ys, Templates[index].Xs, Templates[index].Ys, pairing);
				const float sum = matcher.Match(pairing, limit);
				if (sum < limit)
				{
					standings.Offer({ index, sum, toTemplate }, names);
				}
			}
		}
		else
		{
			static const std::vector<Turn> CoarseTurns = MakeCoarseTurns();
			const Turn finer[3] = { { Cos22_5, Sin22_5 }, { Cos11_25, Sin11_25 }, { Cos5_625, Sin5_625 } };

			// The strokes at each coarse turn, made once for every template
			std::vector<std::vector<float>> coarseXs(CoarseTurns.size());
			std::vector<std::vector<float>> coarseYs(CoarseTurns.size());
			std::vector<uint8_t> coarseValid(CoarseTurns.size());
			for (size_t turn = 0; turn < CoarseTurns.size(); ++turn)
			{
				coarseValid[turn] = prepareTurn(CoarseTurns[turn], coarseXs[turn], coarseYs[turn]) ? 1 : 0;
			}

			std::vector<Pairing> pairings(CoarseTurns.size());
			std::vector<size_t> byBound;
			std::vector<float> xs;
			std::vector<float> ys;
			Pairing finePairing;
			for (size_t index = 0; index < Templates.size(); ++index)
			{
				const Template& entry = Templates[index];
				float best = standings.GetLimit(entry.Name, names);
				std::optional<Turn> bestTurn;

				// Every coarse turn's bound, then the turns matched from the least bound up until a
				// bound can't beat the best: as exact as matching all of them, and mostly only one
				byBound.clear();
				for (size_t turn = 0; turn < CoarseTurns.size(); ++turn)
				{
					if (coarseValid[turn])
					{
						matcher.Prepare(coarseXs[turn], coarseYs[turn], entry.Xs, entry.Ys, pairings[turn]);
						byBound.push_back(turn);
					}
				}
				std::stable_sort(byBound.begin(), byBound.end(), [&](size_t a, size_t b)
				{
					return pairings[a].Bound < pairings[b].Bound;
				});
				for (const size_t turn : byBound)
				{
					if (!(pairings[turn].Bound < best))
					{
						break;
					}
					const float sum = matcher.Match(pairings[turn], best);
					if (sum < best)
					{
						best = sum;
						bestTurn = CoarseTurns[turn];
					}
				}

				// Halving the step either side of the best: to within about 3 degrees. A template no
				// coarse turn brought under the limit is left there.
				for (const Turn step : finer)
				{
					if (!bestTurn)
					{
						break;
					}
					const Turn around = *bestTurn;
					for (const Turn turn : { Compose(around, step), Compose(around, Inverse(step)) })
					{
						if (prepareTurn(turn, xs, ys))
						{
							matcher.Prepare(xs, ys, entry.Xs, entry.Ys, finePairing);
							const float sum = matcher.Match(finePairing, best);
							if (sum < best)
							{
								best = sum;
								bestTurn = turn;
							}
						}
					}
				}
				if (bestTurn)
				{
					standings.Offer({ index, best, *bestTurn }, names);
				}
			}
		}

		const float weightSum = matcher.GetWeightSum();
		const auto toMatch = [&](const Standings::Entry& entry)
		{
			PointCloudMatch match;
			match.Name = Templates[entry.Template].Name;
			match.Template = entry.Template;
			match.Distance = entry.Sum / weightSum;
			match.Score = std::max(0.0f, 1.0f - match.Distance / MaxScoredDistance);
			// The strokes were turned by TurnToTemplate; the template's +Y turned back
			match.Up = Apply(Inverse(entry.TurnToTemplate), glm::vec2(0.0f, 1.0f));
			return match;
		};

		PointCloudResult result;
		if (standings.Best)
		{
			result.Best = toMatch(*standings.Best);
		}
		if (standings.RunnerUp)
		{
			result.RunnerUp = toMatch(*standings.RunnerUp);
		}
		return result;
	}

	std::vector<glm::vec2> MakePointCloud(const std::vector<Stroke>& strokes, uint32_t pointCount)
	{
		const std::vector<glm::vec2> centred = MakeCentredCloud(strokes, pointCount);
		std::vector<glm::vec2> points;
		if (centred.empty() || !TurnAndScale(centred, Turn{}, points))
		{
			return {};
		}
		return points;
	}

	bool ParseStrokeTemplates(std::string_view text, std::vector<StrokeTemplate>& outTemplates, std::string& outError)
	{
		outTemplates.clear();
		outError.clear();

		const auto fail = [&](size_t lineNumber, const std::string& problem)
		{
			outError = "line " + std::to_string(lineNumber) + ": " + problem;
			return false;
		};
		const auto checkLastHasStrokes = [&](size_t lineNumber)
		{
			if (!outTemplates.empty() && outTemplates.back().Strokes.empty())
			{
				const std::string name = outTemplates.back().Name;
				outTemplates.pop_back();
				return fail(lineNumber, "template '" + name + "' has no strokes");
			}
			return true;
		};

		size_t lineNumber = 0;
		while (!text.empty())
		{
			++lineNumber;
			const size_t end = text.find('\n');
			std::string_view line = Trim(text.substr(0, end));
			text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
			if (line.empty() || line.front() == '#')
			{
				continue;
			}

			const std::string_view keyword = TakeWord(line);
			if (keyword == "template")
			{
				if (!checkLastHasStrokes(lineNumber - 1))
				{
					return false;
				}
				const std::string_view name = Trim(line);
				if (name.empty())
				{
					return fail(lineNumber, "a template needs a name");
				}
				outTemplates.push_back(StrokeTemplate{ std::string(name), {} });
			}
			else if (keyword == "stroke")
			{
				if (outTemplates.empty())
				{
					return fail(lineNumber, "a stroke before any template");
				}
				std::vector<float> numbers;
				for (std::string_view word = TakeWord(line); !word.empty(); word = TakeWord(line))
				{
					float value = 0.0f;
					const std::from_chars_result result = std::from_chars(word.data(), word.data() + word.size(), value);
					if (result.ec != std::errc() || result.ptr != word.data() + word.size() || !std::isfinite(value))
					{
						return fail(lineNumber, "'" + std::string(word) + "' is not a number");
					}
					numbers.push_back(value);
				}
				if (numbers.empty() || numbers.size() % 2 != 0)
				{
					return fail(lineNumber, "a stroke needs x y pairs, " + std::to_string(numbers.size()) + " numbers given");
				}
				Stroke stroke;
				for (size_t index = 0; index < numbers.size(); index += 2)
				{
					stroke.push_back(glm::vec2(numbers[index], numbers[index + 1]));
				}
				outTemplates.back().Strokes.push_back(std::move(stroke));
			}
			else
			{
				return fail(lineNumber, "expected 'template' or 'stroke', found '" + std::string(keyword) + "'");
			}
		}
		return checkLastHasStrokes(lineNumber);
	}

	std::string WriteStrokeTemplates(const std::vector<StrokeTemplate>& templates)
	{
		std::string text;
		for (const StrokeTemplate& entry : templates)
		{
			text += "template " + entry.Name + "\n";
			for (const Stroke& stroke : entry.Strokes)
			{
				text += "stroke";
				for (const glm::vec2& point : stroke)
				{
					text += ' ';
					AppendNumber(text, point.x);
					text += ' ';
					AppendNumber(text, point.y);
				}
				text += '\n';
			}
		}
		return text;
	}

	bool LoadStrokeTemplates(const std::filesystem::path& path, std::vector<StrokeTemplate>& outTemplates, std::string& outError)
	{
		outTemplates.clear();
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			outError = "can't read '" + path.string() + "'";
			return false;
		}
		std::ostringstream text;
		text << file.rdbuf();
		if (!ParseStrokeTemplates(text.str(), outTemplates, outError))
		{
			outError = path.string() + ": " + outError;
			return false;
		}
		return true;
	}
}
