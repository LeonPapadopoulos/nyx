// Live edits: messages made from one world and applied to another, as the editor and the game do.
#include "BinaryArchive.h"
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "EditorLinkMessages.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "LiveEdits.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}

	// Remembers which assets were asked for; the tests have no renderer to load them
	class TestAssetResolver final : public IAssetResolver
	{
	public:
		Nyx::Mesh* ResolveMesh(const std::string& meshId) override
		{
			MeshRequests.push_back(meshId);
			return nullptr;
		}

		Nyx::Material* ResolveMaterial(const std::string& materialId) override
		{
			MaterialRequests.push_back(materialId);
			return nullptr;
		}

		std::vector<std::string> MeshRequests;
		std::vector<std::string> MaterialRequests;
	};

	template <typename TComponent>
	const ComponentTypeOps& GetOps()
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<TComponent>());
		Require(ops != nullptr, "A component type isn't registered");
		return *ops;
	}

	template <typename TComponent>
	size_t GetPropertyIndex(const char* name)
	{
		const std::optional<size_t> index = Nyx::Reflection::FindPropertyIndexByName(Nyx::Reflection::GetTypeMetadata<TComponent>(), name);
		Require(index.has_value(), std::string("No property ") + name);
		return *index;
	}

	template <typename TMessage>
	ELiveEditResult Apply(const TMessage& message, Registry& world, ScenePostLoadContext postLoadContext = {})
	{
		return ApplyLiveEdit(MakeNetMessage(message), world, postLoadContext);
	}

	template <typename TMessage>
	EditorLinkMessageText Describe(const TMessage& message, bool bWithDetails = true)
	{
		return DescribeEditorLinkMessage(MakeNetMessage(message), bWithDetails);
	}

	EntityGuid GetGuid(const Registry& world, Entity entity)
	{
		return world.Get<GuidComponent>(entity).Guid;
	}

	Entity AddEntity(Registry& world, const std::string& name)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, TransformComponent{});
		return entity;
	}

	// Every entity, as scene files store it, by guid. Two worlds hold the same entities when these are equal.
	std::map<uint64_t, std::vector<std::byte>> GetEntitiesByGuid(const Registry& world)
	{
		std::map<uint64_t, std::vector<std::byte>> entities;
		world.ForEachEntity([&](Entity entity)
			{
				BinaryWriter writer;
				Require(SceneSerializer::WriteEntity(world, entity, writer), "An entity can't be written");
				Require(entities.emplace(GetGuid(world, entity).Value, writer.GetBytes()).second, "Two entities have the same guid");
			});
		return entities;
	}

	size_t CountEntities(const Registry& world)
	{
		size_t count = 0;
		world.ForEachEntity([&](Entity) { ++count; });
		return count;
	}

	// The game starts with a copy of the editor's world, as Play saves the scene and the game loads it
	void CopyWorld(const Registry& from, Registry& to, ScenePostLoadContext postLoadContext = {})
	{
		from.ForEachEntity([&](Entity entity)
			{
				const std::optional<CreateEntityMessage> message = MakeCreateEntityMessage(from, entity);
				Require(message && Apply(*message, to, postLoadContext) == ELiveEditResult::Applied, "An entity can't be copied");
			});
	}

	// How many properties a property block holds, and the name hash of the first
	uint16_t CountProperties(const std::vector<std::byte>& properties, uint32_t* outFirstNameHash = nullptr)
	{
		BinaryReader reader;
		reader.LoadFromMemory(properties);
		BinaryReader block;
		uint16_t count = 0;
		Require(reader.ReadBlock(block) && block.ReadUInt16(count), "A property block can't be read");
		if (outFirstNameHash && count > 0)
		{
			Require(block.ReadUInt32(*outFirstNameHash), "A property block can't be read");
		}
		return count;
	}

	void TestMessagesReadBackAsWritten()
	{
		SetPropertiesMessage setProperties;
		setProperties.Entity = EntityGuid{ 0x1122334455667788ull };
		setProperties.ComponentType = "Nyx::Engine::TransformComponent";
		BinaryWriter block;
		BinaryWriter content;
		content.WriteUInt16(0);
		block.WriteBlock(content);
		setProperties.Properties = block.GetBytes();

		SetPropertiesMessage readSetProperties;
		Require(ReadNetMessage(MakeNetMessage(setProperties), readSetProperties) && readSetProperties.Entity == setProperties.Entity &&
				readSetProperties.ComponentType == setProperties.ComponentType && readSetProperties.Properties == setProperties.Properties,
			"SetProperties changed on the way");

		CreateEntityMessage createEntity;
		createEntity.Entity = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
		CreateEntityMessage readCreateEntity;
		Require(ReadNetMessage(MakeNetMessage(createEntity), readCreateEntity) && readCreateEntity.Entity == createEntity.Entity,
			"CreateEntity changed on the way");

		DeleteEntityMessage readDeleteEntity;
		Require(ReadNetMessage(MakeNetMessage(DeleteEntityMessage{ EntityGuid{ 42 } }), readDeleteEntity) && readDeleteEntity.Entity.Value == 42,
			"DeleteEntity changed on the way");

		// Cut off anywhere, they can't be read
		const Nyx::Net::Message whole = MakeNetMessage(setProperties);
		for (size_t size = 0; size < whole.Payload.size(); ++size)
		{
			Nyx::Net::Message cut = whole;
			cut.Payload.resize(size);
			SetPropertiesMessage ignored;
			Require(!ReadNetMessage(cut, ignored), "A cut-off SetProperties was read");
		}

		Nyx::Net::Message cutEntity = MakeNetMessage(createEntity);
		cutEntity.Payload.pop_back();
		Require(!ReadNetMessage(cutEntity, readCreateEntity), "A cut-off CreateEntity was read");
	}

	// Only the properties asked for, each once, in declaration order, and only saved ones
	void TestSerializeProperties()
	{
		TransformComponent transform;
		const auto& type = Nyx::Reflection::GetTypeMetadata<TransformComponent>();
		const size_t position = GetPropertyIndex<TransformComponent>("Position");
		const size_t scale = GetPropertyIndex<TransformComponent>("Scale");

		BinaryWriter writer;
		Require(ReflectedArchiveSerializer::SerializeProperties(writer, &transform, type, { scale, position, scale, 99 }),
			"SerializeProperties failed");

		uint32_t firstNameHash = 0;
		Require(CountProperties(writer.GetBytes(), &firstNameHash) == 2 && firstNameHash == type.Properties[position].NameHash,
			"SerializeProperties wrote the wrong properties");

		// A property that isn't saved isn't written either
		struct TwoFloats
		{
			float Saved = 1.0f;
			float NotSaved = 2.0f;
		};
		using namespace Nyx::Reflection;
		const PropertyMetadata twoFloatsProperties[] = {
			{ .Name = "Saved", .NameHash = 0x1111, .Kind = EPropertyKind::Float, .Flags = EPropertyFlags::Serialize, .Offset = offsetof(TwoFloats, Saved) },
			{ .Name = "NotSaved", .NameHash = 0x2222, .Kind = EPropertyKind::Float, .Flags = EPropertyFlags::Edit | EPropertyFlags::Undo,
				.Offset = offsetof(TwoFloats, NotSaved) },
		};
		const TypeMetadata twoFloatsType{ .Name = "TwoFloats", .Properties = twoFloatsProperties, .PropertyCount = 2 };
		const TwoFloats twoFloats;
		BinaryWriter onlySaved;
		Require(ReflectedArchiveSerializer::SerializeProperties(onlySaved, &twoFloats, twoFloatsType, { 0, 1 }) &&
				CountProperties(onlySaved.GetBytes(), &firstNameHash) == 1 && firstNameHash == 0x1111,
			"SerializeProperties wrote a property that isn't saved");

		// Read like a whole object: what the block doesn't have keeps its value
		TransformComponent target;
		target.Rotation = glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
		transform.Position = glm::vec3(1.0f, 2.0f, 3.0f);
		transform.Scale = glm::vec3(4.0f);
		BinaryWriter changed;
		Require(ReflectedArchiveSerializer::SerializeProperties(changed, &transform, type, { position, scale }), "SerializeProperties failed");
		BinaryReader reader;
		reader.LoadFromMemory(changed.GetBytes());
		ReadWarnings warnings;
		Require(ReflectedArchiveSerializer::DeserializeObject(reader, &target, type, warnings), "The block can't be read");
		Require(target.Position == transform.Position && target.Scale == transform.Scale && target.Rotation == glm::quat(0.0f, 1.0f, 0.0f, 0.0f),
			"Reading some properties changed others");
	}

	// The editor moves a cube and changes a camera; the game's copies follow
	void TestSetProperties()
	{
		Registry editor;
		const Entity cube = AddEntity(editor, "Cube");
		const Entity camera = AddEntity(editor, "Camera");
		editor.Add<CameraComponent>(camera, CameraComponent{});

		Registry game;
		CopyWorld(editor, game);
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The game's copy differs from the start");

		// Only Position is sent, so the scale changed without an edit stays in the game
		auto& transform = editor.Get<TransformComponent>(cube);
		transform.Position = glm::vec3(1.0f, 2.0f, 3.0f);
		transform.Scale = glm::vec3(5.0f);

		const std::optional<SetPropertiesMessage> move =
			MakeSetPropertiesMessage(editor, cube, GetOps<TransformComponent>(), { GetPropertyIndex<TransformComponent>("Position") });
		Require(move && move->Entity == GetGuid(editor, cube) && move->ComponentType == "Nyx::Engine::TransformComponent" &&
				CountProperties(move->Properties) == 1,
			"The move isn't one property of the cube's transform");
		Require(Apply(*move, game) == ELiveEditResult::Applied, "The move wasn't applied");

		const Entity gameCube = *FindEntityByGuid(game, GetGuid(editor, cube));
		Require(game.Get<TransformComponent>(gameCube).Position == glm::vec3(1.0f, 2.0f, 3.0f), "The game's cube didn't move");
		Require(game.Get<TransformComponent>(gameCube).Scale == glm::vec3(1.0f), "A property that wasn't sent changed");

		transform.Scale = glm::vec3(1.0f);
		editor.Get<CameraComponent>(camera).FovYDegrees = 75.0f;
		const std::optional<SetPropertiesMessage> zoom =
			MakeSetPropertiesMessage(editor, camera, GetOps<CameraComponent>(), { GetPropertyIndex<CameraComponent>("FovYDegrees") });
		Require(zoom && Apply(*zoom, game) == ELiveEditResult::Applied, "The field of view wasn't applied");
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The game's copy differs after the edits");

		// Nothing to send: no property that is saved, no such component, or no guid
		Require(!MakeSetPropertiesMessage(editor, cube, GetOps<TransformComponent>(), { 99 }), "A message without properties was made");
		Require(!MakeSetPropertiesMessage(editor, cube, GetOps<TransformComponent>(), {}), "A message without properties was made");
		Require(!MakeSetPropertiesMessage(editor, cube, GetOps<CameraComponent>(), { 0 }), "A message for a missing component was made");
		const Entity withoutGuid = editor.CreateEntity();
		editor.Add<TransformComponent>(withoutGuid, TransformComponent{});
		Require(!MakeSetPropertiesMessage(editor, withoutGuid, GetOps<TransformComponent>(), { 0 }) &&
				!MakeCreateEntityMessage(editor, withoutGuid),
			"A message for an entity without a guid was made");

		// Values for a component the game's entity doesn't have are ignored: the others would be missing
		editor.Add<CameraComponent>(cube, CameraComponent{ .FovYDegrees = 40.0f });
		const std::optional<SetPropertiesMessage> addCamera =
			MakeSetPropertiesMessage(editor, cube, GetOps<CameraComponent>(), { GetPropertyIndex<CameraComponent>("FovYDegrees") });
		Require(addCamera && Apply(*addCamera, game) == ELiveEditResult::Ignored && !game.Has<CameraComponent>(gameCube),
			"Values for a component the game's entity doesn't have weren't ignored");
	}

	// Added, deleted and brought back, with the assets the game has to load
	void TestCreateAndDeleteEntities()
	{
		Registry editor;
		AddEntity(editor, "Floor");
		Registry game;
		TestAssetResolver resolver;
		const ScenePostLoadContext postLoadContext{ &resolver };
		CopyWorld(editor, game, postLoadContext);

		const Entity cube = AddEntity(editor, "Cube");
		auto& meshRenderer = editor.Add<MeshRendererComponent>(cube, MeshRendererComponent{});
		meshRenderer.Mesh = AssetReference{ "Mesh", "Cube" };
		meshRenderer.Material = AssetReference{ "Material", "Default" };

		const std::optional<CreateEntityMessage> create = MakeCreateEntityMessage(editor, cube);
		Require(create && Apply(*create, game, postLoadContext) == ELiveEditResult::Applied, "The new entity wasn't created");
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The game's new entity differs");
		Require(resolver.MeshRequests == std::vector<std::string>{ "Cube" } && resolver.MaterialRequests == std::vector<std::string>{ "Default" },
			"The new entity's assets weren't looked up");

		// A changed mesh is looked up again
		meshRenderer.Mesh.Path = "Sphere";
		const std::optional<SetPropertiesMessage> changeMesh =
			MakeSetPropertiesMessage(editor, cube, GetOps<MeshRendererComponent>(), { GetPropertyIndex<MeshRendererComponent>("Mesh") });
		Require(changeMesh && Apply(*changeMesh, game, postLoadContext) == ELiveEditResult::Applied && resolver.MeshRequests.back() == "Sphere",
			"The changed mesh wasn't looked up");
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The game's entity differs after the mesh changed");

		// The same entity again, changed: it replaces the game's, which keeps one entity per guid
		meshRenderer.bVisible = false;
		const std::optional<CreateEntityMessage> again = MakeCreateEntityMessage(editor, cube);
		Require(again && Apply(*again, game, postLoadContext) == ELiveEditResult::Applied && CountEntities(game) == 2,
			"An entity that the game had already wasn't replaced");
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The replaced entity differs");

		const EntityGuid cubeGuid = GetGuid(editor, cube);
		editor.DestroyEntity(cube);
		Require(Apply(DeleteEntityMessage{ cubeGuid }, game) == ELiveEditResult::Applied && !FindEntityByGuid(game, cubeGuid),
			"The entity wasn't deleted");
		Require(GetEntitiesByGuid(editor) == GetEntitiesByGuid(game), "The worlds differ after deleting");

		// Undo brings it back as it was
		Require(Apply(*again, game, postLoadContext) == ELiveEditResult::Applied && FindEntityByGuid(game, cubeGuid) &&
				!game.Get<MeshRendererComponent>(*FindEntityByGuid(game, cubeGuid)).bVisible,
			"The entity didn't come back");
	}

	// What can't be applied is ignored or reported, and leaves the world as it was
	void TestWhatCantBeApplied()
	{
		Registry editor;
		const Entity cube = AddEntity(editor, "Cube");
		Registry game;
		CopyWorld(editor, game);
		const auto before = GetEntitiesByGuid(game);

		editor.Get<TransformComponent>(cube).Position = glm::vec3(9.0f);
		SetPropertiesMessage move =
			*MakeSetPropertiesMessage(editor, cube, GetOps<TransformComponent>(), { GetPropertyIndex<TransformComponent>("Position") });

		SetPropertiesMessage unknownEntity = move;
		unknownEntity.Entity = EntityGuid{ 12345 };
		Require(Apply(unknownEntity, game) == ELiveEditResult::Ignored, "An edit of an unknown entity wasn't ignored");

		SetPropertiesMessage unknownType = move;
		unknownType.ComponentType = "Nyx::Engine::NoSuchComponent";
		Require(Apply(unknownType, game) == ELiveEditResult::Ignored, "An edit of an unknown component type wasn't ignored");

		Require(Apply(DeleteEntityMessage{ EntityGuid{ 12345 } }, game) == ELiveEditResult::Ignored, "Deleting an unknown entity wasn't ignored");
		Require(ApplyLiveEdit(MakeNetMessage(QuitMessage{}), game, {}) == ELiveEditResult::NotALiveEdit, "Quit was taken for a live edit");

		Require(GetEntitiesByGuid(game) == before, "Ignored edits changed the world");

		// A property block that says it has more properties than it holds
		BinaryWriter damagedContent;
		damagedContent.WriteUInt16(3);
		BinaryWriter damagedBlock;
		damagedBlock.WriteBlock(damagedContent);
		SetPropertiesMessage damaged = move;
		damaged.Properties = damagedBlock.GetBytes();
		Require(Apply(damaged, game) == ELiveEditResult::Unreadable, "A damaged property block wasn't reported");

		// Unreadable payloads
		for (const EEditorLinkMessage type : { EEditorLinkMessage::SetProperties, EEditorLinkMessage::CreateEntity, EEditorLinkMessage::DeleteEntity })
		{
			Require(ApplyLiveEdit(Nyx::Net::Message{ static_cast<uint16_t>(type), { std::byte{ 1 } } }, game, {}) == ELiveEditResult::Unreadable,
				"An unreadable message wasn't reported");
		}

		// A damaged entity, or one without a guid, leaves nothing behind
		CreateEntityMessage create = *MakeCreateEntityMessage(editor, cube);
		create.Entity.resize(create.Entity.size() - 2);
		Require(Apply(create, game) == ELiveEditResult::Unreadable && CountEntities(game) == 1, "A damaged entity was kept");

		Registry other;
		const Entity withoutGuid = other.CreateEntity();
		other.Add<TransformComponent>(withoutGuid, TransformComponent{});
		BinaryWriter entity;
		Require(SceneSerializer::WriteEntity(other, withoutGuid, entity), "An entity can't be written");
		CreateEntityMessage noGuid;
		noGuid.Entity = entity.GetBytes();
		Require(Apply(noGuid, game) == ELiveEditResult::Ignored && CountEntities(game) == 1, "An entity without a guid was kept");

		// A property the game's build doesn't have is skipped; the others are applied
		BinaryWriter mixedContent;
		mixedContent.WriteUInt16(2);
		mixedContent.WriteUInt32(0xDEADBEEF);
		mixedContent.WriteUInt8(static_cast<uint8_t>(Nyx::Reflection::EPropertyKind::Float));
		mixedContent.WriteFloat(1.0f);
		mixedContent.WriteUInt32(Nyx::Reflection::GetTypeMetadata<TransformComponent>().Properties[GetPropertyIndex<TransformComponent>("Position")].NameHash);
		mixedContent.WriteUInt8(static_cast<uint8_t>(Nyx::Reflection::EPropertyKind::Vec3));
		ReflectedArchiveSerializer::WriteValue(mixedContent, glm::vec3(7.0f, 8.0f, 9.0f));
		BinaryWriter mixedBlock;
		mixedBlock.WriteBlock(mixedContent);
		SetPropertiesMessage mixed = move;
		mixed.Properties = mixedBlock.GetBytes();
		Require(Apply(mixed, game) == ELiveEditResult::Applied &&
				game.Get<TransformComponent>(*FindEntityByGuid(game, move.Entity)).Position == glm::vec3(7.0f, 8.0f, 9.0f),
			"A property the game knows wasn't applied next to one it doesn't");
	}

	void TestDescriptions()
	{
		Registry editor;
		const Entity cube = AddEntity(editor, "Cube");
		editor.Get<TransformComponent>(cube).Position = glm::vec3(1.0f, 2.0f, 3.0f);
		auto& meshRenderer = editor.Add<MeshRendererComponent>(cube, MeshRendererComponent{});
		meshRenderer.Mesh = AssetReference{ "Mesh", "Cube" };
		const std::string guid = ReflectedArchivePrinter::ValueToText(GetGuid(editor, cube).Value);

		const SetPropertiesMessage move =
			*MakeSetPropertiesMessage(editor, cube, GetOps<TransformComponent>(), { GetPropertyIndex<TransformComponent>("Position") });
		const EditorLinkMessageText moveText = Describe(move);
		Require(moveText.Name == "SetProperties" && moveText.Summary == guid + " TransformComponent: Position: (1, 2, 3)" &&
				Contains(moveText.Details, "Entity: " + guid) && Contains(moveText.Details, "ComponentType: \"Nyx::Engine::TransformComponent\"") &&
				Contains(moveText.Details, "\n  Position: (1, 2, 3)"),
			"SetProperties is described wrongly: " + moveText.Summary + "\n" + moveText.Details);
		Require(Describe(move, false).Details.empty() && Describe(move, false).Summary == moveText.Summary,
			"Without details, the summary differs or details are there");

		// Structs on one line, in braces
		const SetPropertiesMessage mesh = *MakeSetPropertiesMessage(editor, cube, GetOps<MeshRendererComponent>(),
			{ GetPropertyIndex<MeshRendererComponent>("Mesh"), GetPropertyIndex<MeshRendererComponent>("bVisible") });
		const EditorLinkMessageText meshText = Describe(mesh);
		Require(meshText.Summary == guid + " MeshRendererComponent: Mesh {Type: \"Mesh\", Path: \"Cube\"}, bVisible: true",
			"Structs aren't summarized on one line: " + meshText.Summary);

		const EditorLinkMessageText createText = Describe(*MakeCreateEntityMessage(editor, cube));
		Require(createText.Name == "CreateEntity" && Contains(createText.Summary, guid + " \"Cube\" (") &&
				Contains(createText.Summary, "MeshRendererComponent") && Contains(createText.Summary, "TransformComponent") &&
				Contains(createText.Details, "  Nyx::Engine::MeshRendererComponent\n") && Contains(createText.Details, "      Path: \"Cube\""),
			"CreateEntity is described wrongly: " + createText.Summary + "\n" + createText.Details);
		Require(Describe(*MakeCreateEntityMessage(editor, cube), false).Details.empty(), "CreateEntity has details without asking");

		const EditorLinkMessageText deleteText = Describe(DeleteEntityMessage{ GetGuid(editor, cube) });
		Require(deleteText.Name == "DeleteEntity" && deleteText.Summary == guid && deleteText.Details == "Entity: " + guid,
			"DeleteEntity is described wrongly: " + deleteText.Summary);

		const EditorLinkMessageText broken =
			DescribeEditorLinkMessage(Nyx::Net::Message{ static_cast<uint16_t>(EEditorLinkMessage::CreateEntity), { std::byte{ 9 } } });
		Require(broken.Name == "CreateEntity" && Contains(broken.Summary, "can't be read"), "An unreadable CreateEntity is described wrongly");

		// Summaries of what this build doesn't know (e.g. sent by a newer one), and of empty structs
		const auto writeStruct = [](BinaryWriter& block, uint32_t nameHash, const BinaryWriter& content)
		{
			block.WriteUInt32(nameHash);
			block.WriteUInt8(static_cast<uint8_t>(Nyx::Reflection::EPropertyKind::Struct));
			block.WriteBlock(content);
		};
		BinaryWriter unknownStruct;
		unknownStruct.WriteUInt16(1);
		unknownStruct.WriteUInt32(0x12345678);
		unknownStruct.WriteUInt8(static_cast<uint8_t>(Nyx::Reflection::EPropertyKind::Float));
		unknownStruct.WriteFloat(2.0f);
		BinaryWriter emptyStruct;
		emptyStruct.WriteUInt16(0);
		BinaryWriter content;
		content.WriteUInt16(4);
		writeStruct(content, 0xDEADBEEF, unknownStruct);
		writeStruct(content, 0xCAFEBABE, emptyStruct);
		writeStruct(content, Nyx::Reflection::GetTypeMetadata<MeshRendererComponent>().Properties[GetPropertyIndex<MeshRendererComponent>("Mesh")].NameHash,
			emptyStruct);
		content.WriteUInt32(Nyx::Reflection::GetTypeMetadata<MeshRendererComponent>().Properties[GetPropertyIndex<MeshRendererComponent>("bVisible")].NameHash);
		content.WriteUInt8(static_cast<uint8_t>(Nyx::Reflection::EPropertyKind::Bool));
		content.WriteBool(true);
		BinaryWriter block;
		block.WriteBlock(content);
		SetPropertiesMessage unusual = mesh;
		unusual.Properties = block.GetBytes();
		const std::string unusualSummary = Describe(unusual).Summary;
		Require(unusualSummary == guid + " MeshRendererComponent: 0xDEADBEEF: (Struct, not a property of this type) {0x12345678: 2 (Float)}, "
				"0xCAFEBABE: (Struct, not a property of this type), Mesh {}, bVisible: true",
			"Unusual properties are summarized wrongly: " + unusualSummary);

		CreateEntityMessage noComponents;
		noComponents.Entity = { std::byte{ 1 }, std::byte{ 0 } };
		Require(Describe(noComponents).Summary == "0x0000000000000000 (damaged)", "A damaged entity is summarized wrongly: " + Describe(noComponents).Summary);
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		RegisterComponentTypes();

		TestMessagesReadBackAsWritten();
		TestSerializeProperties();
		TestSetProperties();
		TestCreateAndDeleteEntities();
		TestWhatCantBeApplied();
		TestDescriptions();

		std::cout << "All live edit tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Live edit test failed: " << error.what() << '\n';
		return 1;
	}
}
