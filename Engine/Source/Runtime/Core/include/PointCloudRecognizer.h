#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shapes drawn by hand, told apart by a point-cloud recogniser of the $P family (Vatavu et al.,
// "Gestures as Point Clouds", 2012): the strokes become a cloud of points, resampled,
// scaled into a unit box and moved to their centroid, and matched against named template clouds by
// a greedy match of their points. A cloud has no order, so it doesn't matter in which order or
// direction the strokes were drawn, nor how many there were.
namespace Nyx
{
	// One stroke drawn by hand: its points in the order they were drawn, in any units, +Y up
	using Stroke = std::vector<glm::vec2>;

	struct PointCloudOptions
	{
		// Every cloud is resampled to this many points, at least 2. More tell finer shapes apart
		// and cost more: a match is about PointCount^2.5 steps.
		uint32_t PointCount = 32;

		// Whether a shape turned is another shape, e.g. an arrow to the right and one up. On, the
		// strokes are matched as drawn, with the up given to Recognize() as the templates' +Y. Off,
		// they are matched at whatever turn fits each template best, to within about 3 degrees, and
		// the match says where the template's up lay; that costs about four times more.
		bool bRotationSensitive = false;
	};

	// How well some strokes match one template
	struct PointCloudMatch
	{
		std::string Name;

		// Which template matched: its index in the order they were added
		size_t Template = 0;

		// 1 for the same cloud, down to 0 at a Distance of MaxScoredDistance and beyond
		float Score = 0.0f;

		// The mean distance between matched points, weighted as $P weights them, in units of the
		// unit box the clouds are scaled into: about 0.03 for a shape drawn again with some care,
		// 0.1 and more for another shape
		float Distance = 0.0f;

		// Where the template's +Y lay in the strokes as drawn (unit length): the up given when
		// rotation sensitive, else the turn that fitted best, e.g. which way an arrow points
		glm::vec2 Up{ 0.0f, 1.0f };
	};

	struct PointCloudResult
	{
		// The closest template; none without templates or for strokes of no length. Close or not:
		// what is close enough (its Score) is the caller's to say.
		std::optional<PointCloudMatch> Best;

		// The closest template of another name, the one the best was told apart from
		std::optional<PointCloudMatch> RunnerUp;
	};

	// Templates as named point clouds, several per name (a few examples of a shape drawn by
	// different hands), and the strokes they match. Deterministic: the same templates and strokes
	// give the same result to the bit on any machine running the same build (no trigonometry of a
	// C library; ties go to the template added first).
	class PointCloudRecognizer
	{
	public:
		// Where Score reaches 0
		static constexpr float MaxScoredDistance = 0.25f;

		explicit PointCloudRecognizer(PointCloudOptions options = {});

		const PointCloudOptions& GetOptions() const
		{
			return Options;
		}

		// Adds an example of a shape, +Y up. False, and nothing added, for strokes of no length
		// (no points, or only one place).
		bool AddTemplate(std::string name, const std::vector<Stroke>& strokes);

		size_t GetTemplateCount() const
		{
			return Templates.size();
		}

		const std::string& GetTemplateName(size_t index) const
		{
			return Templates[index].Name;
		}

		void ClearTemplates()
		{
			Templates.clear();
		}

		// The closest templates to the strokes. up is the strokes' up when rotation sensitive (any
		// length; a zero one is +Y), and unused otherwise.
		PointCloudResult Recognize(const std::vector<Stroke>& strokes, glm::vec2 up = { 0.0f, 1.0f }) const;

	private:
		struct Template
		{
			std::string Name;
			// The cloud's points, x and y apart
			std::vector<float> Xs;
			std::vector<float> Ys;
		};

		PointCloudOptions Options;
		std::vector<Template> Templates;
	};

	// The cloud $P matches: pointCount points spread evenly along the strokes (the gaps between
	// strokes don't count), scaled alike in x and y so that their box's longer side is 1, and moved so
	// that their centroid is at the origin. Empty for strokes of no length, or a pointCount under 2.
	std::vector<glm::vec2> MakePointCloud(const std::vector<Stroke>& strokes, uint32_t pointCount);

	// Templates as a game ships them, in a text file of lines like these:
	//
	//     # Comments and empty lines are skipped
	//     template Arrow
	//     stroke 0 0  1 0
	//     stroke 0.8 0.2  1 0  0.8 -0.2
	//
	// "template" starts one template, named by the rest of its line; each "stroke" line below it is
	// one of its strokes, its points as x y pairs in drawing order, +Y up, in any units. A name may
	// have several templates. Numbers are written so that they read back exactly.
	struct StrokeTemplate
	{
		std::string Name;
		std::vector<Stroke> Strokes;
	};

	// False, with the first problem and its line in outError, if the text isn't in the format above
	// or a template has no strokes; outTemplates then has the templates before the problem.
	bool ParseStrokeTemplates(std::string_view text, std::vector<StrokeTemplate>& outTemplates, std::string& outError);

	std::string WriteStrokeTemplates(const std::vector<StrokeTemplate>& templates);

	// ParseStrokeTemplates() of a file's text; outError names the file
	bool LoadStrokeTemplates(const std::filesystem::path& path, std::vector<StrokeTemplate>& outTemplates, std::string& outError);
}
