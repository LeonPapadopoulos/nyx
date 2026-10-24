// Reflected assets (ReflectedAssetText, ReflectedAssetTypes) on the test module's creature, a game's own
// asset type with every shape: its text written and read back, value by value; a hand-written file's
// comments and unknown lines kept through edits, named entries renamed, taken away, put in above
// others and given in one line; values that can't be read and names it doesn't have listed at their
// properties; the type's own checks and its entries' names; the registry; a game listening for live
// text, which reaches it over a real editor link as an AssetValues message.
#include "EditorLink.h"
#include "EditorLinkMessageText.h"
#include "GameModule.h"
#include "LiveEdits.h"
#include "Log.h"
#include "NetConnection.h"
#include "ReflectedAssetTypes.h"
#include "ReflectionUtils.h"
#include "TestCreatureAsset.h"
#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace NyxTestModule;
	using Nyx::Tests::Require;

	TestCreatureData MakeHound()
	{
		TestCreatureData hound;
		hound.DisplayName = "Marsh Hound";
		hound.Legs = 6;
		hound.Litter = 3;
		hound.bNocturnal = true;
		hound.Speed = 4.5f;
		hound.Diet = ETestDiet::Meat;
		hound.Tint = glm::vec3(0.4f, 0.3f, 0.2f);
		hound.Size = glm::vec2(1.5f, 0.75f);
		hound.Markings = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
		hound.Portrait = AssetReference{ .Type = "Material", .Path = "Materials/Hound.nyxmat" };
		hound.Habitats = { "Swamp", "Reed, Bank" };
		hound.Prey = { ETestDiet::Plants, ETestDiet::Meat };
		hound.Senses.Sight = 30.0f;
		hound.Moves = { TestCreatureMove{ .Name = "Bite", .Damage = 12.0f, .Reach = 1.5f, .Tags = { "Quick", "Night" } }, TestCreatureMove{ .Name = "Howl", .Damage = 0.0f } };
		hound.Resistances = { TestCreatureResistance{ .Name = "Fire", .Amount = 0.5f } };
		hound.Ranges = { TestCreatureRange{ .Name = "Hearing", .Default = 20.0f, .Min = 5.0f, .Max = 40.0f } };
		hound.Waypoints = { TestCreatureWaypoint{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) }, TestCreatureWaypoint{ .Wait = 2.0f } };
		hound.Skins = { AssetReference{ .Type = "Material", .Path = "Materials/A.nyxmat" }, AssetReference{ .Type = "Material", .Path = "Materials/B.nyxmat" } };
		hound.Hunger = 0.9f;
		return hound;
	}

	const char* HoundText = "DisplayName = Marsh Hound\n"
							"Legs = 6\n"
							"Litter = 3\n"
							"bNocturnal = true\n"
							"Speed = 4.5\n"
							"Diet = Meat\n"
							"Tint = 0.4 0.3 0.2\n"
							"Size = 1.5 0.75\n"
							"Markings = 1 0 0 0.5\n"
							"Portrait = Materials/Hound.nyxmat\n"
							"Habitats = Swamp, \"Reed, Bank\"\n"
							"Prey = Plants, Meat\n"
							"Senses.Sight = 30\n"
							"Move.Bite.Damage = 12\n"
							"Move.Bite.Reach = 1.5\n"
							"Move.Bite.Tags = Quick, Night\n"
							"Move.Howl.Damage = 0\n"
							"Resist.Fire = 0.5\n"
							"Range.Hearing = 20 5 40\n"
							"Waypoints[0].Position = 1 2 3\n"
							"Waypoints[1].Wait = 2\n"
							"Skins = Materials/A.nyxmat, Materials/B.nyxmat\n";

	bool SameReference(const AssetReference& a, const AssetReference& b)
	{
		return a.Type == b.Type && a.Path == b.Path;
	}

	// Every value the file has; Hunger isn't one
	void RequireSameCreature(const TestCreatureData& read, const TestCreatureData& written, const std::string& what)
	{
		Require(read.DisplayName == written.DisplayName && read.Legs == written.Legs && read.Litter == written.Litter && read.bNocturnal == written.bNocturnal &&
				read.Speed == written.Speed && read.Diet == written.Diet,
			what + ": its numbers, words and enum");
		Require(read.Tint == written.Tint && read.Size == written.Size && read.Markings == written.Markings, what + ": its vectors and colour");
		Require(SameReference(read.Portrait, written.Portrait), what + ": its asset reference, with its type");
		Require(read.Habitats == written.Habitats && read.Prey == written.Prey, what + ": its lists");
		Require(read.Senses.Sight == written.Senses.Sight && read.Senses.Smell == written.Senses.Smell, what + ": its struct");
		Require(read.Moves.size() == written.Moves.size(), what + ": its moves");
		for (size_t i = 0; i < read.Moves.size(); ++i)
		{
			Require(read.Moves[i].Name == written.Moves[i].Name && read.Moves[i].Damage == written.Moves[i].Damage && read.Moves[i].Reach == written.Moves[i].Reach &&
					read.Moves[i].Tags == written.Moves[i].Tags,
				what + ": move " + written.Moves[i].Name);
		}
		Require(read.Resistances.size() == written.Resistances.size(), what + ": its resistances");
		for (size_t i = 0; i < read.Resistances.size(); ++i)
		{
			Require(read.Resistances[i].Name == written.Resistances[i].Name && read.Resistances[i].Amount == written.Resistances[i].Amount, what + ": a resistance");
		}
		Require(read.Ranges.size() == written.Ranges.size(), what + ": its ranges");
		for (size_t i = 0; i < read.Ranges.size(); ++i)
		{
			Require(read.Ranges[i].Name == written.Ranges[i].Name && read.Ranges[i].Default == written.Ranges[i].Default && read.Ranges[i].Min == written.Ranges[i].Min &&
					read.Ranges[i].Max == written.Ranges[i].Max,
				what + ": range " + written.Ranges[i].Name);
		}
		Require(read.Waypoints.size() == written.Waypoints.size(), what + ": its waypoints");
		for (size_t i = 0; i < read.Waypoints.size(); ++i)
		{
			Require(read.Waypoints[i].Position == written.Waypoints[i].Position && read.Waypoints[i].Wait == written.Waypoints[i].Wait, what + ": a waypoint");
		}
		Require(read.Skins.size() == written.Skins.size() && std::equal(read.Skins.begin(), read.Skins.end(), written.Skins.begin(), SameReference), what + ": its list of references");
	}

	bool HasProblem(const std::vector<PropertyProblem>& problems, const std::string& path, const std::string& part = "")
	{
		return std::any_of(problems.begin(), problems.end(), [&](const PropertyProblem& problem)
			{
				return problem.Path == path && problem.Message.find(part) != std::string::npos;
			});
	}

	std::string Describe(const std::vector<PropertyProblem>& problems)
	{
		std::string text;
		for (const PropertyProblem& problem : problems)
		{
			text += "\n  " + problem.Path + ": " + problem.Message;
		}
		return text;
	}

	void TestEveryShapeRoundTrips(const std::filesystem::path& directory)
	{
		const TestCreatureData hound = MakeHound();
		ReflectedAssetText text;
		text.Write(hound);
		Require(text.ToText() == HoundText, "Written, the hound should be:\n" + text.ToText());

		std::vector<PropertyProblem> problems;
		RequireSameCreature(text.Read<TestCreatureData>(&problems), hound, "Read back");
		Require(problems.empty(), "Read back, nothing should be wrong:" + Describe(problems));

		// Through a file
		const std::filesystem::path file = directory / "Hound.nyxcreature";
		std::string error;
		Require(text.Save(file, error), "The hound should be saved: " + error);
		const ReflectedAssetType& type = *ReflectedAssetTypes::Get().FindByTypeId("TestCreature");
		TestCreatureData loaded;
		Require(LoadReflectedAsset(file, type, &loaded, problems, error) && problems.empty(), "The hound should load: " + error);
		RequireSameCreature(loaded, hound, "Loaded");

		// Written again unchanged: not a line changes
		text.Write(loaded);
		Require(text.ToText() == HoundText, "Written again, the text should be the same:\n" + text.ToText());

		// A new entry writes the values it doesn't have at their defaults, or its first if it has all
		TestCreatureData plain;
		plain.Moves = { TestCreatureMove{ .Name = "Sniff" } };
		ReflectedAssetText sniff;
		sniff.Write(plain);
		Require(sniff.ToText() == "Move.Sniff.Damage = 1\n" && sniff.Read<TestCreatureData>().Moves.size() == 1, "An entry at its defaults should be one line:\n" + sniff.ToText());

		// A creature at its defaults writes nothing; WriteAll writes every value
		ReflectedAssetText empty;
		empty.Write(TestCreatureData{});
		Require(empty.ToText().empty(), "Defaults should write no lines:\n" + empty.ToText());
		empty.WriteAll(&loaded, type.Struct);
		Require(empty.ToText().find("Senses.Smell = 0.5\n") != std::string::npos && empty.ToText().find("Litter = 3\n") != std::string::npos,
			"WriteAll should write the defaults too:\n" + empty.ToText());
		RequireSameCreature(empty.Read<TestCreatureData>(), hound, "Written whole");

		// Words that need quotes: spaces at the ends, a quote first, a line break
		TestCreatureData quoted;
		quoted.DisplayName = " Spaced ";
		quoted.Habitats = { "\"Quoted\"", "", "Line\nBreak" };
		ReflectedAssetText quotes;
		quotes.Write(quoted);
		Require(quotes.ToText() == "DisplayName = \" Spaced \"\nHabitats = \"\\\"Quoted\\\"\", \"\", \"Line\\nBreak\"\n", "Quoted words:\n" + quotes.ToText());
		ReflectedAssetText parsed;
		Require(parsed.Parse(quotes.ToText(), error), "The quotes should parse: " + error);
		const TestCreatureData unquoted = parsed.Read<TestCreatureData>();
		Require(unquoted.DisplayName == quoted.DisplayName && unquoted.Habitats == quoted.Habitats, "Quoted words should read back as they were");
	}

	const char* HandWritten = "# A marsh hound, by hand\n"
							  "DisplayName = Marsh Hound\n"
							  "Mood = grumpy\n"
							  "\n"
							  "# Its moves\n"
							  "Move.Bite.Damage = 12.0\n"
							  "# The bite's reach\n"
							  "Move.Bite.Reach = 1.5\n"
							  "Move.Howl.Damage = 0\n"
							  "Resist.Fire = 0.5\n"
							  "Resist.Cold = 0.25\n"
							  "Range.Hearing = 20 5\n";

	void TestEditsKeepTheFile()
	{
		ReflectedAssetText text;
		std::string error;
		Require(text.Parse(HandWritten, error), "The hand-written hound should parse: " + error);
		std::vector<PropertyProblem> problems;
		TestCreatureData hound = text.Read<TestCreatureData>(&problems);
		Require(problems.empty(), "An unknown line isn't a problem:" + Describe(problems));
		Require(hound.Moves.size() == 2 && hound.Moves[0].Name == "Bite" && hound.Moves[0].Damage == 12.0f && hound.Moves[1].Reach == 1.0f,
			"The moves, a value not written at its default");
		Require(hound.Ranges.size() == 1 && hound.Ranges[0].Default == 20.0f && hound.Ranges[0].Min == 5.0f && hound.Ranges[0].Max == 1.0f,
			"A line of fewer values keeps the rest at their defaults");

		// Unchanged: as written, "12.0" too
		text.Write(hound);
		Require(text.ToText() == HandWritten, "Written unchanged, the text should be as written:\n" + text.ToText());

		// A value changed, an entry renamed, another taken away, one put in above another
		hound.Moves[0].Damage = 14.0f;
		hound.Moves[1].Name = "Yowl";
		hound.Resistances.erase(hound.Resistances.begin());
		hound.Ranges.insert(hound.Ranges.begin(), TestCreatureRange{ .Name = "Sight", .Default = 30.0f, .Min = 10.0f, .Max = 60.0f });
		hound.Ranges[1].Max = 40.0f;
		text.Write(hound);
		const std::string expected = "# A marsh hound, by hand\n"
									 "DisplayName = Marsh Hound\n"
									 "Mood = grumpy\n"
									 "\n"
									 "# Its moves\n"
									 "Move.Bite.Damage = 14\n"
									 "# The bite's reach\n"
									 "Move.Bite.Reach = 1.5\n"
									 "Move.Yowl.Damage = 0\n"
									 "Resist.Cold = 0.25\n"
									 "Range.Sight = 30 10 60\n"
									 "Range.Hearing = 20 5 40\n";
		Require(text.ToText() == expected, "The edits should change their own lines:\n" + text.ToText());
		RequireSameCreature(text.Read<TestCreatureData>(), hound, "Edited");

		// Moved down: the lines follow, where the moves' lines began; a comment between them stays after
		// them
		std::swap(hound.Moves[0], hound.Moves[1]);
		text.Write(hound);
		Require(text.ToText().find("# Its moves\nMove.Yowl.Damage = 0\nMove.Bite.Damage = 14\nMove.Bite.Reach = 1.5\n# The bite's reach\n") !=
					std::string::npos,
			"Moved, the entries' lines should be in their order:\n" + text.ToText());
		RequireSameCreature(text.Read<TestCreatureData>(), hound, "Moved");

		// Every entry and element gone: their lines go, the comments stay
		hound.Moves.clear();
		hound.Ranges.clear();
		text.Write(hound);
		Require(text.ToText().find("Move.") == std::string::npos && text.ToText().find("Range.") == std::string::npos &&
				text.ToText().find("# The bite's reach\n") != std::string::npos && text.ToText().find("Mood = grumpy\n") != std::string::npos,
			"The entries' lines should go, comments and unknown lines stay:\n" + text.ToText());

		// A name twice: the last line wins, and once written there is one line
		Require(text.Parse("Speed = 1\nSpeed = 2\n", error) && text.Read<TestCreatureData>().Speed == 2.0f, "The last line of a name should win");
		TestCreatureData faster;
		faster.Speed = 3.0f;
		text.Write(faster);
		Require(text.ToText() == "Speed = 3\n", "Written, a name twice should be one line:\n" + text.ToText());
	}

	void TestProblems()
	{
		ReflectedAssetText text;
		std::string error;
		Require(text.Parse("Legs = many\n"
						   "Diet = Pebbles\n"
						   "Tint = 1 2\n"
						   "Move.Bite.Damage = hard\n"
						   "Move.Bad Name.Damage = 1\n"
						   "Move.Howl.Loudness = 3\n"
						   "Senses.Taste = 1\n"
						   "Senses = 1\n"
						   "Range.Hearing = 1 2 3 4\n"
						   "Waypoints.First.Wait = 1\n"
						   "Habitats = \"Open\n"
						   "Resist.Fire.Amount = 1\n"
						   "bNocturnal = maybe\n"
						   "Mood = grumpy\n",
					 error),
			"The text should parse: " + error);
		std::vector<PropertyProblem> problems;
		const TestCreatureData creature = text.Read<TestCreatureData>(&problems);
		Require(problems.size() == 13, "Every line but the unknown one should be a problem:" + Describe(problems));
		Require(HasProblem(problems, "Legs", "isn't a whole number") && HasProblem(problems, "Diet", "isn't one of Plants, Meat, Anything") &&
				HasProblem(problems, "Tint", "isn't 3 numbers") && HasProblem(problems, "bNocturnal", "isn't true or false"),
			"Values that can't be read, at their properties:" + Describe(problems));
		Require(HasProblem(problems, "Moves.Bite.Damage", "isn't a number") && HasProblem(problems, "Moves", "by name") &&
				HasProblem(problems, "Moves.Howl", "has no \"Loudness\"") && HasProblem(problems, "Senses", "has no \"Taste\"") &&
				HasProblem(problems, "Senses", "a line for each") && HasProblem(problems, "Ranges.Hearing", "4 values, 3 at most") &&
				HasProblem(problems, "Waypoints", "by index") && HasProblem(problems, "Habitats", "item 1") &&
				HasProblem(problems, "Resistances.Fire.Amount", "has no \"Amount\""),
			"Names it doesn't have, at the property they stopped at:" + Describe(problems));
		Require(creature.Legs == 4 && creature.Diet == ETestDiet::Plants && creature.Tint == glm::vec3(1.0f) && creature.Habitats.empty(),
			"Values that can't be read should keep their defaults");
		Require(creature.Moves.empty() && creature.Resistances.empty() && creature.Ranges.empty() && creature.Waypoints.empty(),
			"An entry only a line that can't be read names shouldn't be made");

		// The type's own checks and the entries' names, at their properties
		const ReflectedAssetType& type = *ReflectedAssetTypes::Get().FindByTypeId("TestCreature");
		TestCreatureData odd;
		odd.Speed = -1.0f;
		odd.Ranges = { TestCreatureRange{ .Name = "Sight", .Min = 5.0f, .Max = 1.0f } };
		odd.Moves = { TestCreatureMove{ .Name = "Bite" }, TestCreatureMove{ .Name = "Bite" }, TestCreatureMove{}, TestCreatureMove{ .Name = "Two words" } };
		const std::vector<PropertyProblem> checked = ValidateReflectedAsset(type, &odd);
		Require(checked.size() == 5 && HasProblem(checked, "Speed", "below 0") && HasProblem(checked, "Ranges.Sight.Min", "above Max") &&
				HasProblem(checked, "Moves.Bite.Name", "another entry") && HasProblem(checked, "Moves[2].Name", "needs a name") &&
				HasProblem(checked, "Moves[3].Name", "letters, digits and _"),
			"The creature's problems:" + Describe(checked));

		// Entries that can't be written are left out of the text
		ReflectedAssetText written;
		written.Write(odd);
		Require(written.ToText().find("Move.Bite.Damage = 1\n") != std::string::npos && written.ToText().find("Two words") == std::string::npos &&
				written.Read<TestCreatureData>().Moves.size() == 1,
			"Only the first Bite should be written:\n" + written.ToText());

		// Names of new entries
		std::vector<TestCreatureMove> moves = { TestCreatureMove{ .Name = "Entry" }, TestCreatureMove{ .Name = "Entry2" } };
		const Reflection::ArrayMetadata& array = *Reflection::FindPropertyByName(Reflection::GetTypeMetadata<TestCreatureData>(), "Moves")->Array;
		Require(MakeEntryName(&moves, array, "Entry") == "Entry3" && MakeEntryName(&moves, array, "Bite") == "Bite", "A new entry's name should be one none has");
		Require(FindEntryNameProperty(array) && !FindEntryNameProperty(*Reflection::FindPropertyByName(Reflection::GetTypeMetadata<TestCreatureData>(), "Waypoints")->Array),
			"Moves are named entries, waypoints elements by index");
		Require(GetElementPath("Moves", &moves[1], &Reflection::GetTypeMetadata<TestCreatureMove>(), 1) == "Moves.Entry2" &&
				GetElementPath("Habitats", nullptr, nullptr, 2) == "Habitats[2]",
			"Elements' paths");
	}

	void TestTheRegistry()
	{
		ReflectedAssetTypes& types = ReflectedAssetTypes::Get();
		const ReflectedAssetType* creature = types.FindByExtension(".NyxCreature");
		Require(creature && creature->DisplayName == "Test Creature" && types.FindByExtension("nyxcreature") == creature &&
				types.FindForPath("Creatures/Hound.nyxcreature") == creature && types.FindByTypeId("TestCreature") == creature,
			"The module's type should be found by extension, path and type id");
		Require(types.FindByExtension(".nyxphysmat") && !types.FindForPath("Creatures/Hound") && !types.FindByExtension(".nyxnothing"),
			"The engine's type, and none for others");

		// Another struct can't take its extension; the same type again replaces it
		const size_t count = types.GetAll().size();
		types.Register(ReflectedAssetDesc<TestCreatureSenses>{ .TypeId = "Senses", .DisplayName = "Senses", .Extension = ".nyxcreature" });
		Require(types.GetAll().size() == count && types.FindByExtension(".nyxcreature") == creature && !types.FindByTypeId("Senses"),
			"A type taking another's extension should be refused");
		types.Register(ReflectedAssetDesc<TestCreatureData>{
			.TypeId = "TestCreature", .DisplayName = "Test Creature", .Extension = ".nyxcreature", .Description = creature->Description, .Validate = {} });
		Require(types.GetAll().size() == count && !types.FindByTypeId("TestCreature")->Validate, "Registered again, the type should be replaced");
		GetLinkedGameModule().RegisterTypes();
		Require(static_cast<bool>(types.FindByTypeId("TestCreature")->Validate), "The module's registration back");
	}

	// Calls step until done() is true, for at most the time limit
	bool PumpUntil(const std::function<void()>& step, const std::function<bool()>& done, int timeLimitMs = 10000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			step();
			if (done())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	void TestLiveTextReachesTheGame()
	{
		// Who listens for a type takes its live text; once it stops, no one does
		std::vector<std::string> taken;
		{
			const ReflectedAssetListener listener(TestCreatureExtension, [&taken](const std::string& path, std::string_view text)
				{
					taken.push_back(path + ": " + std::string(text));
				});
			Require(ReflectedAssetTypes::Get().TakeLiveText("Creatures/Hound.nyxcreature", "Speed = 5\n") && taken.size() == 1 &&
					taken[0] == "Creatures/Hound.nyxcreature: Speed = 5\n",
				"The listener should take the creature's text");
			Require(!ReflectedAssetTypes::Get().TakeLiveText("PhysicsMaterials/Wood.nyxphysmat", "Friction = 1\n") && taken.size() == 1,
				"Another type's text shouldn't reach it");
		}
		Require(!ReflectedAssetTypes::Get().TakeLiveText("Creatures/Hound.nyxcreature", "Speed = 6\n") && taken.size() == 1, "Gone, it should take nothing");

		// Over a real link: the editor sends, the game applies what it receives
		const ReflectedAssetListener game(TestCreatureExtension, [&taken](const std::string& path, std::string_view text)
			{
				TestCreatureData creature;
				ReflectedAssetText values;
				std::string error;
				Require(values.Parse(text, error), "The live text should parse: " + error);
				values.Read(&creature, Reflection::GetTypeMetadata<TestCreatureData>());
				taken.push_back(path + " runs at " + std::to_string(static_cast<int>(creature.Speed)));
			});
		Net::Listener listener;
		Require(listener.Listen(0), "Listening failed: " + listener.GetError());
		EditorLink gameEnd(Net::Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame");
		std::unique_ptr<EditorLink> editorEnd;
		const auto update = [&]()
		{
			gameEnd.Update();
			if (!editorEnd)
			{
				if (std::unique_ptr<Net::Connection> accepted = listener.Accept())
				{
					editorEnd = std::make_unique<EditorLink>(std::move(accepted), "NyxEditor");
				}
			}
			if (editorEnd)
			{
				editorEnd->Update();
			}
		};
		Require(PumpUntil(update, [&] { return gameEnd.IsConnected() && editorEnd && editorEnd->IsConnected(); }), "The link should connect");

		const AssetValuesMessage values{ "Creatures/Hound.nyxcreature", "# Live\nSpeed = 7\n" };
		Require(DescribeEditorLinkMessage(MakeNetMessage(values)).Summary == "Creatures/Hound.nyxcreature, 2 lines", "AssetValues should be described by its path");
		editorEnd->Send(values);
		editorEnd->Send(AssetValuesMessage{ "Dogs/Rex.nyxdog", "Bark = 1\n" });
		std::vector<ELiveEditResult> results;
		Require(PumpUntil(
					[&]
					{
						update();
						while (std::optional<Net::Message> message = gameEnd.Receive())
						{
							results.push_back(ApplyAssetValues(*message));
						}
					},
					[&] { return results.size() == 2; }),
			"The game should receive both messages");
		Require(results[0] == ELiveEditResult::Applied && taken.back() == "Creatures/Hound.nyxcreature runs at 7", "The creature's values should reach its listener");
		Require(results[1] == ELiveEditResult::Ignored, "A type nobody listens for should be ignored");
		Require(ApplyAssetValues(MakeNetMessage(QuitMessage{})) == ELiveEditResult::NotALiveEdit, "Another message isn't asset values");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterEngineAssetTypes();
		// The test module registers its creature, as a game's module registers its types
		Nyx::GetLinkedGameModule().RegisterTypes();

		Nyx::Tests::TestDirectory directory("NyxReflectedAssetTests");
		TestEveryShapeRoundTrips(directory.Path);
		TestEditsKeepTheFile();
		TestProblems();
		TestTheRegistry();
		TestLiveTextReachesTheGame();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Reflected asset test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All reflected asset tests passed.\n";
	return 0;
}
