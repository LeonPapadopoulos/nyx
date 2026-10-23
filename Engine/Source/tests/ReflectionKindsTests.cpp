// Enum and Array properties at run time: the header tool's enum_and_array fixture, compiled with the
// code the tool generates for it (its golden file), read and written through PropertyValue, the
// archive serializer and its printer.
#include "BinaryArchive.h"
#include "Log.h"
#include "PropertyValueAccess.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"

#include "Input.h"
#include "Expected.reflect.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Reflection;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	const TypeMetadata& TestType()
	{
		return GetTypeMetadata<EnumAndArrayTest>();
	}

	const PropertyMetadata& PropertyNamed(const char* name)
	{
		const PropertyMetadata* property = FindPropertyByName(TestType(), name);
		Require(property != nullptr, std::string("No property ") + name);
		return *property;
	}

	EnumAndArrayTest MakeFilled()
	{
		EnumAndArrayTest object;
		object.Shape = EDeadZoneShape::Hybrid;
		object.Floats = { 0.5f, -1.0f };
		object.Points = { { 1.0f, 2.0f }, { 3.0f, 4.0f }, { 5.0f, 6.0f } };
		object.Signs = { ESigned::Below, ESigned::Zero };
		object.Elements = { { "first" }, { "second" } };
		return object;
	}

	bool Equal(const EnumAndArrayTest& a, const EnumAndArrayTest& b)
	{
		const auto sameElements = [](const std::vector<ArrayElement>& x, const std::vector<ArrayElement>& y)
		{
			if (x.size() != y.size())
			{
				return false;
			}
			for (size_t i = 0; i < x.size(); ++i)
			{
				if (x[i].Name != y[i].Name)
				{
					return false;
				}
			}
			return true;
		};
		return a.Shape == b.Shape && a.Floats == b.Floats && a.Points == b.Points && a.Signs == b.Signs && sameElements(a.Elements, b.Elements);
	}

	// One property block with one property, as a newer or older build might have saved it
	BinaryReader BlockWith(uint32_t nameHash, EPropertyKind kind, const std::function<void(BinaryWriter&)>& writeValue)
	{
		BinaryWriter block;
		block.WriteUInt16(1);
		block.WriteUInt32(nameHash);
		block.WriteUInt8(static_cast<uint8_t>(kind));
		writeValue(block);

		BinaryWriter data;
		data.WriteBlock(block);
		BinaryReader reader;
		reader.LoadFromMemory(data.GetBytes());
		return reader;
	}

	void TestEnumMetadata()
	{
		const EnumMetadata& shape = GetEnumMetadata<EDeadZoneShape>();
		Require(shape.EntryCount == 4 && shape.Size == 1 && !shape.bSigned, "EDeadZoneShape: 4 enumerators, 1 byte, unsigned");
		Require(std::string(shape.DisplayName) == "Dead Zone Shape", "Its display name");
		Require(FindEnumEntry(shape, 6) && std::string(FindEnumEntry(shape, 6)->Name) == "Hybrid", "Hybrid counts on from ScaledRadial = 5");
		Require(FindEnumEntryByName(shape, "Radial")->Value == 1, "Find by name");
		Require(EnumValueToText(shape, 3) == "3", "A number no enumerator has shows as the number");

		const EnumMetadata& sign = GetEnumMetadata<ESigned>();
		Require(sign.Size == 2 && sign.bSigned, "ESigned: 2 bytes, signed");
		const ESigned below = ESigned::Below;
		Require(ReadEnumValue(&below, sign) == -2, "A negative enumerator reads back negative");

		EnumAndArrayTest object;
		const PropertyMetadata& property = PropertyNamed("Shape");
		Require(property.Kind == EPropertyKind::Enum && TryGetEnum(property) == &shape, "Shape is an Enum property with its enumerators");
		Require(std::get<int32_t>(GetPropertyValue(&object, property)) == 5, "Its value is the enumerator's number, as an Int32");
		SetPropertyValue(&object, property, int32_t(1));
		Require(object.Shape == EDeadZoneShape::Radial, "Setting the number sets the enumerator");
	}

	void TestArrayMetadata()
	{
		EnumAndArrayTest object = MakeFilled();
		const PropertyMetadata& points = PropertyNamed("Points");
		const ArrayMetadata* array = TryGetArray(points);
		Require(array && array->Element.Kind == EPropertyKind::Vec2, "Points is an Array of Vec2");
		Require(GetArraySize(&object, points) == 3, "Three points");

		void* vector = GetPropertyAddress(&object, points);
		Require(std::get<glm::vec2>(GetPropertyValue(array->GetElement(vector, 1), array->Element)) == glm::vec2(3.0f, 4.0f),
			"An element is a property of itself at offset 0");
		array->Insert(vector, 0);
		array->Swap(vector, 0, 1);
		array->Erase(vector, 3);
		Require(object.Points.size() == 3 && object.Points[0] == glm::vec2(1.0f, 2.0f) && object.Points[1] == glm::vec2(0.0f),
			"Insert, swap and erase work on the vector");

		const ArrayMetadata* elements = TryGetArray(PropertyNamed("Elements"));
		Require(elements && TryGetNestedType(elements->Element) == &GetTypeMetadata<ArrayElement>(), "Struct elements know their type");
		const ArrayMetadata* signs = TryGetArray(PropertyNamed("Signs"));
		Require(signs && TryGetEnum(signs->Element) == &GetEnumMetadata<ESigned>(), "Enum elements know their enumerators");
	}

	void TestRoundTrip()
	{
		const EnumAndArrayTest written = MakeFilled();
		BinaryWriter writer;
		Require(ReflectedArchiveSerializer::SerializeObject(writer, &written, TestType()), "Writes");

		BinaryReader reader;
		reader.LoadFromMemory(writer.GetBytes());
		EnumAndArrayTest read;
		read.Floats = { 9.0f, 9.0f, 9.0f, 9.0f };
		ReadWarnings warnings;
		Require(ReflectedArchiveSerializer::DeserializeObject(reader, &read, TestType(), warnings), "Reads");
		Require(Equal(read, written), "Reads back what was written; arrays take the saved length");

		// The printer shows enumerators by name and arrays element by element
		BinaryReader printReader;
		printReader.LoadFromMemory(writer.GetBytes());
		std::string text;
		Require(ReflectedArchivePrinter::PrintObject(printReader, &TestType(), 0, text), "Prints");
		Require(text.find("Shape: Hybrid") != std::string::npos, "Shape by name:\n" + text);
		Require(text.find("Points: 3 Vec2s") != std::string::npos && text.find("  [2]: (5, 6)") != std::string::npos, "Points:\n" + text);
		Require(text.find("  [0]: Below") != std::string::npos, "Enum elements by name:\n" + text);
		Require(text.find("  [1]:\n    Name: \"second\"") != std::string::npos, "Struct elements indented:\n" + text);

		// An empty array is written and read as empty
		EnumAndArrayTest empty;
		BinaryWriter emptyWriter;
		Require(ReflectedArchiveSerializer::SerializeObject(emptyWriter, &empty, TestType()), "Writes empty arrays");
		BinaryReader emptyReader;
		emptyReader.LoadFromMemory(emptyWriter.GetBytes());
		EnumAndArrayTest filled = MakeFilled();
		Require(ReflectedArchiveSerializer::DeserializeObject(emptyReader, &filled, TestType(), warnings) && filled.Points.empty(),
			"Reading an empty array empties it");
	}

	void TestKindChanges()
	{
		ReadWarnings warnings;

		// An Int32 saved where an Enum is now: converted, when an enumerator has the number
		EnumAndArrayTest object;
		BinaryReader asInt = BlockWith(PropertyNamed("Shape").NameHash, EPropertyKind::Int32, [](BinaryWriter& w)
			{
				w.WriteInt32(6);
			});
		Require(ReflectedArchiveSerializer::DeserializeObject(asInt, &object, TestType(), warnings) && object.Shape == EDeadZoneShape::Hybrid,
			"An Int32 6 becomes Hybrid");

		// A number no enumerator has keeps the value
		BinaryReader unknown = BlockWith(PropertyNamed("Shape").NameHash, EPropertyKind::Enum, [](BinaryWriter& w)
			{
				w.WriteInt32(3);
			});
		Require(ReflectedArchiveSerializer::DeserializeObject(unknown, &object, TestType(), warnings) && object.Shape == EDeadZoneShape::Hybrid,
			"An unknown number keeps the value");

		// Int32 elements saved where Float elements are now: converted
		BinaryReader ints = BlockWith(PropertyNamed("Floats").NameHash, EPropertyKind::Array,
			[](BinaryWriter& w)
			{
				BinaryWriter array;
				array.WriteUInt8(static_cast<uint8_t>(EPropertyKind::Int32));
				array.WriteUInt32(2);
				array.WriteInt32(3);
				array.WriteInt32(-4);
				w.WriteBlock(array);
			});
		Require(ReflectedArchiveSerializer::DeserializeObject(ints, &object, TestType(), warnings) && object.Floats == std::vector<float>{ 3.0f, -4.0f },
			"Int32 elements become floats");

		// String elements where Vec2 elements are: skipped, and the rest of the block still reads
		object.Points = { { 7.0f, 7.0f } };
		BinaryReader strings = BlockWith(PropertyNamed("Points").NameHash, EPropertyKind::Array,
			[](BinaryWriter& w)
			{
				BinaryWriter array;
				array.WriteUInt8(static_cast<uint8_t>(EPropertyKind::String));
				array.WriteUInt32(1);
				array.WriteString("not a point");
				w.WriteBlock(array);
			});
		Require(ReflectedArchiveSerializer::DeserializeObject(strings, &object, TestType(), warnings) && object.Points.size() == 1,
			"Elements of another kind are skipped, the array kept");

		// A count larger than the data: damaged
		BinaryReader damaged = BlockWith(PropertyNamed("Floats").NameHash, EPropertyKind::Array,
			[](BinaryWriter& w)
			{
				BinaryWriter array;
				array.WriteUInt8(static_cast<uint8_t>(EPropertyKind::Float));
				array.WriteUInt32(1'000'000);
				w.WriteBlock(array);
			});
		Require(!ReflectedArchiveSerializer::DeserializeObject(damaged, &object, TestType(), warnings), "A count the data can't hold is damaged data");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestEnumMetadata();
		TestArrayMetadata();
		TestRoundTrip();
		TestKindChanges();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Reflection kinds test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All reflection kinds tests passed.\n";
	return 0;
}
