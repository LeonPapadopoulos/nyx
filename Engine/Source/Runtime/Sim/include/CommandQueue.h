#pragma once

#include "ReflectionTypes.h"

#include <cstddef>
#include <vector>

namespace Nyx
{
	// What players ask of the simulation, in the order they asked, for the next step to apply
	// (Docs/Design/simulation-loop.md, "Only the simulation changes the world"). A command is a
	// reflected struct of the game's, e.g.
	//
	//   NYX_REFLECT()
	//   struct PlaceMarker
	//   {
	//       NYX_PROPERTY(Serialize)
	//       Nyx::SurfaceAnchor Where;
	//   };
	//
	// It is kept as its Serialize properties, written as a property block (ReflectedArchiveSerializer),
	// so a command is data only: it names entities by guid, never by Entity handle or pointer, and
	// the same bytes can later be recorded for a replay or sent to another machine. Properties
	// without Serialize don't arrive. A player adds a few a frame, so each is written and read on
	// its own.
	class CommandQueue
	{
	public:
		template<typename T>
		void Add(const T& command)
		{
			Add(&command, Nyx::Reflection::GetTypeMetadata<T>());
		}

		// The same for a command whose type is only known by its metadata
		void Add(const void* command, const Nyx::Reflection::TypeMetadata& type);

		// Calls `function(const T&)` for each command of type T, in the order they were added
		template<typename T, typename Function>
		void ForEach(Function&& function) const
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<T>();
			for (const Entry& entry : Entries)
			{
				T command{};
				if (entry.Type == &type && Read(entry, &command))
				{
					function(static_cast<const T&>(command));
				}
			}
		}

		// How many there are, of any type
		size_t GetCount() const
		{
			return Entries.size();
		}

		bool IsEmpty() const
		{
			return Entries.empty();
		}

		// Keeps the memory, so a queue that is filled and cleared every step stops growing once it
		// has held as many as it gets
		void Clear();

		// Appends the other's commands after its own, and clears the other
		void TakeFrom(CommandQueue& other);

	private:
		struct Entry
		{
			const Nyx::Reflection::TypeMetadata* Type = nullptr;
			// Where its property block is in Bytes
			size_t Offset = 0;
			size_t Size = 0;
		};

		// Reads the entry's property block into a default-made command; false if it can't be read
		bool Read(const Entry& entry, void* outCommand) const;

		std::vector<Entry> Entries;
		std::vector<std::byte> Bytes;
	};
}
