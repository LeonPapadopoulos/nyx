#include "NyxPCH.h"
#include "TerrainSave.h"

#include "BinaryArchive.h"
#include "Log.h"
#include "SdfTerrain.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <tuple>

namespace Nyx
{
	// The archive writes the machine's bytes: little-endian on every machine Nyx runs on
	static_assert(std::endian::native == std::endian::little, "The canonical form of the terrain's edits is little-endian");

	namespace
	{
		constexpr uint32_t Letters = 0x4445544Eu; // NTED
		constexpr uint32_t CheckpointLetters = 0x4B43544Eu; // NTCK

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

	void WriteTerrainEdits(const SdfTerrainComponent& terrain, BinaryWriter& out, uint64_t after)
	{
		const auto first = std::partition_point(terrain.Edits.begin(), terrain.Edits.end(), [&](const SdfTerrainEdit& edit) { return edit.Revision <= after; });
		out.WriteUInt32(Letters);
		out.WriteUInt32(TerrainEditsFormatVersion);
		out.WriteUInt64(uint64_t(terrain.Edits.end() - first));
		for (size_t index = size_t(first - terrain.Edits.begin()); index < terrain.Edits.size(); ++index)
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
			// A bake's brick is in one region: 32 bricks to a region's side
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainEditBounds(terrain.Edits[index], low, high);
			if (terrain.Edits[index].Kind == ESdfTerrainEditKind::Bake)
			{
				low = terrain.Edits[index].Position;
				high = low;
			}
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

	std::vector<glm::ivec3> FindTerrainRegionMismatches(std::span<const TerrainRegionHash> a, std::span<const TerrainRegionHash> b)
	{
		std::map<std::tuple<int, int, int>, std::pair<uint64_t, uint64_t>> both;
		for (const TerrainRegionHash& region : a)
		{
			both[{ region.Region.z, region.Region.y, region.Region.x }].first = region.Hash;
		}
		for (const TerrainRegionHash& region : b)
		{
			both[{ region.Region.z, region.Region.y, region.Region.x }].second = region.Hash;
		}
		std::vector<glm::ivec3> differ;
		for (const auto& [key, hashes] : both)
		{
			if (hashes.first != hashes.second)
			{
				const auto& [z, y, x] = key;
				differ.push_back({ x, y, z });
			}
		}
		return differ;
	}

	void WriteTerrainCheckpoint(const SdfTerrainComponent& terrain, BinaryWriter& out)
	{
		out.WriteUInt32(CheckpointLetters);
		out.WriteUInt32(TerrainCheckpointVersion);
		out.WriteUInt64(GetTerrainGeneratorFingerprint());
		out.WriteUInt64(HashTerrainLayers(terrain));
		out.WriteUInt64(GetTerrainRevision(terrain));
		BinaryWriter edits;
		WriteTerrainEdits(terrain, edits);
		out.WriteBlock(edits);
		const std::vector<TerrainRegionHash> regions = HashTerrainRegions(terrain);
		out.WriteUInt32(static_cast<uint32_t>(regions.size()));
		for (const TerrainRegionHash& region : regions)
		{
			out.WriteInt32(region.Region.x);
			out.WriteInt32(region.Region.y);
			out.WriteInt32(region.Region.z);
			out.WriteUInt64(region.Hash);
		}
	}

	bool SaveTerrainCheckpoint(const SdfTerrainComponent& terrain, const std::filesystem::path& path)
	{
		BinaryWriter out;
		WriteTerrainCheckpoint(terrain, out);
		return out.SaveToFile(path);
	}

	TerrainCheckpointRead ReadTerrainCheckpoint(BinaryReader& in, SdfTerrainComponent& terrain)
	{
		TerrainCheckpointRead read;
		uint32_t letters = 0;
		uint32_t version = 0;
		uint64_t fingerprint = 0;
		uint64_t layers = 0;
		if (!in.ReadUInt32(letters) || letters != CheckpointLetters || !in.ReadUInt32(version) || version == 0 || version > TerrainCheckpointVersion ||
			!in.ReadUInt64(fingerprint) || !in.ReadUInt64(layers) || !in.ReadUInt64(read.Revision))
		{
			return read;
		}
		if (fingerprint != GetTerrainGeneratorFingerprint())
		{
			read.Result = ETerrainCheckpointRead::OtherGenerator;
			return read;
		}
		if (layers != HashTerrainLayers(terrain))
		{
			read.Result = ETerrainCheckpointRead::OtherLayers;
			return read;
		}
		BinaryReader block;
		SdfTerrainComponent loaded = terrain;
		uint32_t regionCount = 0;
		if (!in.ReadBlock(block) || !ReadTerrainEdits(block, loaded.Edits) || block.GetRemainingSize() != 0 || GetTerrainRevision(loaded) != read.Revision ||
			!in.ReadUInt32(regionCount) || regionCount > in.GetRemainingSize() / 20)
		{
			return read;
		}
		std::vector<TerrainRegionHash> written(regionCount);
		for (TerrainRegionHash& region : written)
		{
			if (!in.ReadInt32(region.Region.x) || !in.ReadInt32(region.Region.y) || !in.ReadInt32(region.Region.z) || !in.ReadUInt64(region.Hash))
			{
				return read;
			}
		}
		read.Mismatched = FindTerrainRegionMismatches(written, HashTerrainRegions(loaded));
		for (const glm::ivec3& region : read.Mismatched)
		{
			LOG_WARNING("Terrain: region ({0}, {1}, {2}) of a checkpoint at revision {3} isn't as it was written (generator {4:x})", region.x, region.y, region.z,
				read.Revision, fingerprint);
		}
		terrain.Edits = std::move(loaded.Edits);
		read.Result = ETerrainCheckpointRead::Read;
		return read;
	}

	TerrainCheckpointRead LoadTerrainCheckpoint(const std::filesystem::path& path, SdfTerrainComponent& terrain)
	{
		BinaryReader in;
		if (!in.LoadFromFile(path))
		{
			return {};
		}
		return ReadTerrainCheckpoint(in, terrain);
	}

	bool TakeTerrainEdits(SdfTerrainComponent& terrain, std::span<const SdfTerrainEdit> edits)
	{
		uint64_t latest = GetTerrainRevision(terrain);
		for (const SdfTerrainEdit& edit : edits)
		{
			if (edit.Revision <= latest)
			{
				return false;
			}
			latest = edit.Revision;
		}
		terrain.Edits.insert(terrain.Edits.end(), edits.begin(), edits.end());
		return true;
	}

	namespace
	{
		glm::ivec3 RegionOf(const glm::vec3& point)
		{
			return glm::ivec3(glm::floor(point / TerrainRegionSize));
		}
	}

	void WriteTerrainRegionRepair(const SdfTerrainComponent& host, const glm::ivec3& region, BinaryWriter& out)
	{
		std::vector<size_t> bakes;
		for (size_t index = 0; index < host.Edits.size(); ++index)
		{
			if (host.Edits[index].Kind == ESdfTerrainEditKind::Bake && RegionOf(host.Edits[index].Position) == region)
			{
				bakes.push_back(index);
			}
		}
		out.WriteUInt32(static_cast<uint32_t>(bakes.size()));
		for (const size_t index : bakes)
		{
			WriteEdit(host, index, out);
		}
	}

	bool ApplyTerrainRegionRepair(SdfTerrainComponent& terrain, const glm::ivec3& region, BinaryReader& in)
	{
		uint32_t count = 0;
		if (!in.ReadUInt32(count))
		{
			return false;
		}
		std::vector<std::pair<size_t, std::shared_ptr<const SdfTerrainBakedBrick>>> taken;
		for (uint32_t n = 0; n < count; ++n)
		{
			SdfTerrainEdit bake;
			if (!ReadEdit(in, bake) || bake.Kind != ESdfTerrainEditKind::Bake || RegionOf(bake.Position) != region)
			{
				return false;
			}
			const auto ours = std::lower_bound(terrain.Edits.begin(), terrain.Edits.end(), bake.Revision,
				[](const SdfTerrainEdit& edit, uint64_t revision) { return edit.Revision < revision; });
			if (ours == terrain.Edits.end() || ours->Revision != bake.Revision || ours->Kind != ESdfTerrainEditKind::Bake || ours->Position != bake.Position)
			{
				return false;
			}
			taken.push_back({ size_t(ours - terrain.Edits.begin()), bake.Baked });
		}
		for (auto& [index, baked] : taken)
		{
			terrain.Edits[index].Baked = std::move(baked);
		}
		LOG_WARNING("Terrain: region ({0}, {1}, {2}) differed from the host's at revision {3} (generator {4:x}); its {5} bakes now take the host's values", region.x,
			region.y, region.z, GetTerrainRevision(terrain), GetTerrainGeneratorFingerprint(), taken.size());
		return true;
	}

	size_t BakeTerrainRegion(SdfTerrainComponent& host, const glm::ivec3& region)
	{
		for (const SdfTerrainEdit& edit : host.Edits)
		{
			if (edit.Kind == ESdfTerrainEditKind::Bake && RegionOf(edit.Position) == region)
			{
				return 0;
			}
		}
		// The bricks in the region the edits come into
		const glm::ivec3 first = region * int(TerrainRegionSize / SdfTerrainBakeBrickSize);
		const glm::ivec3 last = first + int(TerrainRegionSize / SdfTerrainBakeBrickSize) - 1;
		std::set<std::tuple<int, int, int>> bricks;
		for (const SdfTerrainEdit& edit : host.Edits)
		{
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainEditBounds(edit, low, high);
			const glm::ivec3 from = glm::max(glm::ivec3(glm::floor(low / SdfTerrainBakeBrickSize)), first);
			const glm::ivec3 to = glm::min(glm::ivec3(glm::floor(high / SdfTerrainBakeBrickSize)), last);
			for (int z = from.z; z <= to.z; ++z)
			{
				for (int y = from.y; y <= to.y; ++y)
				{
					for (int x = from.x; x <= to.x; ++x)
					{
						bricks.insert({ z, y, x });
					}
				}
			}
		}
		for (const auto& [z, y, x] : bricks)
		{
			AppendTerrainBake(host, { x, y, z });
		}
		return bricks.size();
	}
}
