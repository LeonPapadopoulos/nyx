#include "NyxPCH.h"
#include "TerrainSave.h"

#include "BinaryArchive.h"
#include "SdfTerrain.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <span>
#include <tuple>

namespace Nyx
{
	// The archive writes the machine's bytes: little-endian on every machine Nyx runs on
	static_assert(std::endian::native == std::endian::little, "The canonical form of the terrain's edits is little-endian");

	namespace
	{
		constexpr uint32_t Letters = 0x4445544Eu; // NTED

		void WriteVector(BinaryWriter& out, const glm::vec3& value)
		{
			out.WriteFloat(value.x);
			out.WriteFloat(value.y);
			out.WriteFloat(value.z);
		}

		bool ReadVector(BinaryReader& in, glm::vec3& outValue)
		{
			return in.ReadFloat(outValue.x) && in.ReadFloat(outValue.y) && in.ReadFloat(outValue.z) && std::isfinite(outValue.x) && std::isfinite(outValue.y) &&
				   std::isfinite(outValue.z);
		}

		void WriteEdit(const SdfTerrainComponent& terrain, size_t index, BinaryWriter& out)
		{
			const SdfTerrainEdit& edit = terrain.Edits[index];
			out.WriteUInt64(edit.Revision);
			out.WriteUInt8(uint8_t(edit.Kind));
			out.WriteUInt8(uint8_t(edit.Shape));
			out.WriteUInt8(uint8_t(edit.Mode));
			WriteVector(out, edit.Position);
			WriteVector(out, edit.End);
			WriteVector(out, edit.HalfSize);
			WriteVector(out, edit.Rotation);
			out.WriteFloat(edit.Radius);
			out.WriteFloat(edit.BlendRadius);
			out.WriteUInt32(edit.Material);
			if (edit.Kind != ESdfTerrainEditKind::Bake)
			{
				return;
			}
			const std::shared_ptr<const SdfTerrainBakedBrick> baked = GetTerrainBakeValues(terrain, index);
			out.WriteUInt64(baked->Layers);
			for (const float value : baked->Values)
			{
				out.WriteFloat(value);
			}
			out.WriteUInt32(static_cast<uint32_t>(baked->Materials.size()));
			for (const uint32_t material : baked->Materials)
			{
				out.WriteUInt32(material);
			}
		}

		bool ReadEdit(BinaryReader& in, SdfTerrainEdit& outEdit)
		{
			uint8_t kind = 0;
			uint8_t shape = 0;
			uint8_t mode = 0;
			if (!in.ReadUInt64(outEdit.Revision) || !in.ReadUInt8(kind) || !in.ReadUInt8(shape) || !in.ReadUInt8(mode) || !ReadVector(in, outEdit.Position) ||
				!ReadVector(in, outEdit.End) || !ReadVector(in, outEdit.HalfSize) || !ReadVector(in, outEdit.Rotation) || !in.ReadFloat(outEdit.Radius) ||
				!in.ReadFloat(outEdit.BlendRadius) || !in.ReadUInt32(outEdit.Material))
			{
				return false;
			}
			if (kind > uint8_t(ESdfTerrainEditKind::Bake) || shape > uint8_t(ESdfTerrainEditShape::Box) || mode > uint8_t(ESdfTerrainEditMode::Paint) ||
				!std::isfinite(outEdit.Radius) || !std::isfinite(outEdit.BlendRadius))
			{
				return false;
			}
			outEdit.Kind = ESdfTerrainEditKind(kind);
			outEdit.Shape = ESdfTerrainEditShape(shape);
			outEdit.Mode = ESdfTerrainEditMode(mode);
			if (outEdit.Kind != ESdfTerrainEditKind::Bake)
			{
				return true;
			}
			if (glm::floor(outEdit.Position / SdfTerrainBakeBrickSize) * SdfTerrainBakeBrickSize != outEdit.Position)
			{
				return false;
			}
			uint64_t layers = 0;
			std::array<float, SdfTerrainBakedBrick::Count> values;
			uint32_t materialCount = 0;
			if (!in.ReadUInt64(layers))
			{
				return false;
			}
			for (float& value : values)
			{
				if (!in.ReadFloat(value) || !std::isfinite(value))
				{
					return false;
				}
			}
			if (!in.ReadUInt32(materialCount) || (materialCount != 1 && materialCount != SdfTerrainBakedBrick::Count))
			{
				return false;
			}
			std::vector<uint32_t> materials(materialCount);
			for (uint32_t& material : materials)
			{
				if (!in.ReadUInt32(material))
				{
					return false;
				}
			}
			outEdit.Baked = MakeTerrainBakedBrick(values, std::move(materials), layers);
			return true;
		}

		uint64_t HashBytes(uint64_t hash, std::span<const std::byte> bytes)
		{
			for (const std::byte byte : bytes)
			{
				hash = (hash ^ uint64_t(byte)) * 0x100000001B3ull;
			}
			return hash;
		}
	}

	void WriteTerrainEdits(const SdfTerrainComponent& terrain, BinaryWriter& out)
	{
		out.WriteUInt32(Letters);
		out.WriteUInt32(TerrainEditsFormatVersion);
		out.WriteUInt64(terrain.Edits.size());
		for (size_t index = 0; index < terrain.Edits.size(); ++index)
		{
			WriteEdit(terrain, index, out);
		}
	}

	bool ReadTerrainEdits(BinaryReader& in, std::vector<SdfTerrainEdit>& outEdits)
	{
		outEdits.clear();
		uint32_t letters = 0;
		uint32_t version = 0;
		uint64_t count = 0;
		if (!in.ReadUInt32(letters) || letters != Letters || !in.ReadUInt32(version) || version == 0 || version > TerrainEditsFormatVersion || !in.ReadUInt64(count))
		{
			return false;
		}
		// The smallest an edit can be, so a count can't ask for more than there is
		constexpr size_t SmallestEdit = 8 + 3 + 12 * 4 + 4 + 4 + 4;
		if (count > in.GetRemainingSize() / SmallestEdit)
		{
			return false;
		}
		std::vector<SdfTerrainEdit> edits(static_cast<size_t>(count));
		for (size_t index = 0; index < edits.size(); ++index)
		{
			if (!ReadEdit(in, edits[index]) || (index > 0 && edits[index].Revision <= edits[index - 1].Revision))
			{
				return false;
			}
		}
		outEdits = std::move(edits);
		return true;
	}

	std::vector<TerrainRegionHash> HashTerrainRegions(const SdfTerrainComponent& terrain, uint64_t revision)
	{
		// Each edit's bytes once, then each region's hash over its edits' in order
		constexpr int64_t MaxRegions = 4096;
		std::map<std::tuple<int, int, int>, std::vector<uint32_t>> regions;
		std::vector<uint32_t> everywhere;
		std::vector<std::pair<size_t, size_t>> spans;
		BinaryWriter bytes;
		for (size_t index = 0; index < terrain.Edits.size() && terrain.Edits[index].Revision <= revision; ++index)
		{
			const size_t start = bytes.GetBytes().size();
			WriteEdit(terrain, index, bytes);
			spans.push_back({ start, bytes.GetBytes().size() - start });
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainEditBounds(terrain.Edits[index], low, high);
			const glm::ivec3 from(glm::floor(low / TerrainRegionSize));
			const glm::ivec3 to(glm::floor(high / TerrainRegionSize));
			const glm::ivec3 span = to - from + 1;
			if (int64_t(span.x) * span.y * span.z > MaxRegions)
			{
				everywhere.push_back(static_cast<uint32_t>(index));
				continue;
			}
			for (int z = from.z; z <= to.z; ++z)
			{
				for (int y = from.y; y <= to.y; ++y)
				{
					for (int x = from.x; x <= to.x; ++x)
					{
						regions[{ z, y, x }].push_back(static_cast<uint32_t>(index));
					}
				}
			}
		}
		const uint64_t layers = HashTerrainLayers(terrain);
		std::vector<TerrainRegionHash> hashes;
		hashes.reserve(regions.size());
		const std::span<const std::byte> all(bytes.GetBytes());
		for (auto& [key, edits] : regions)
		{
			if (!everywhere.empty())
			{
				std::vector<uint32_t> merged;
				std::merge(edits.begin(), edits.end(), everywhere.begin(), everywhere.end(), std::back_inserter(merged));
				edits = std::move(merged);
			}
			uint64_t hash = 0xCBF29CE484222325ull;
			hash = HashBytes(hash, std::as_bytes(std::span<const uint64_t>(&layers, 1)));
			for (const uint32_t index : edits)
			{
				hash = HashBytes(hash, all.subspan(spans[index].first, spans[index].second));
			}
			const auto& [z, y, x] = key;
			hashes.push_back({ glm::ivec3(x, y, z), hash != 0 ? hash : 1 });
		}
		return hashes;
	}
}
