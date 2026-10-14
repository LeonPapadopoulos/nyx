// The built-in meshes' tangents, without a GPU: on a flat face the tangent runs along rising u and
// the bitangent, cross(normal, tangent) * w, up the image (falling v); on the sphere every tangent
// is of unit length, perpendicular to its normal, and the faces wind counter-clockwise outside.
#include "Mesh.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	// A quad facing +Z with its texture's v rising upwards, like the cube's front face
	void TestFlatFaces()
	{
		Nyx::MeshData quad;
		const glm::vec3 normal(0.0f, 0.0f, 1.0f);
		quad.Vertices = {
			{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, normal },
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, normal },
			{ { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, normal },
			{ { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, normal },
		};
		quad.Indices = { 0, 1, 2, 2, 3, 0 };
		Nyx::ComputeTangents(quad);
		for (const Nyx::Vertex& vertex : quad.Vertices)
		{
			Require(Near(glm::vec3(vertex.Tangent), glm::vec3(1.0f, 0.0f, 0.0f)), "The tangent doesn't run along rising u");
			// v rises upwards, so up the image is down here
			const glm::vec3 bitangent = glm::cross(vertex.Normal, glm::vec3(vertex.Tangent)) * vertex.Tangent.w;
			Require(Near(bitangent, glm::vec3(0.0f, -1.0f, 0.0f)), "The bitangent doesn't point up the image (falling v)");
		}

		// The same face with v falling upwards, as images are usually laid on: the bitangent turns
		for (Nyx::Vertex& vertex : quad.Vertices)
		{
			vertex.UV.y = 1.0f - vertex.UV.y;
		}
		Nyx::ComputeTangents(quad);
		const glm::vec3 bitangent = glm::cross(quad.Vertices[0].Normal, glm::vec3(quad.Vertices[0].Tangent)) * quad.Vertices[0].Tangent.w;
		Require(Near(bitangent, glm::vec3(0.0f, 1.0f, 0.0f)), "With v falling upwards, the bitangent doesn't point up");
	}

	void TestSphere()
	{
		const Nyx::MeshData sphere = Nyx::MakeSphere(0.5f, 16, 32);
		Require(sphere.Vertices.size() == 17 * 33 && sphere.Indices.size() == 16 * 32 * 6, "The sphere has the wrong number of vertices or indices");
		for (const Nyx::Vertex& vertex : sphere.Vertices)
		{
			Require(std::abs(glm::length(vertex.Position) - 0.5f) < 1e-4f, "A vertex isn't on the sphere");
			const glm::vec3 tangent(vertex.Tangent);
			Require(std::abs(glm::length(tangent) - 1.0f) < 1e-3f, "A tangent isn't of unit length");
			Require(std::abs(glm::dot(tangent, vertex.Normal)) < 1e-3f, "A tangent isn't perpendicular to its normal");
			Require(vertex.Tangent.w == 1.0f || vertex.Tangent.w == -1.0f, "A tangent's w isn't +1 or -1");
		}
		// Counter-clockwise seen from outside: each triangle's normal points away from the middle
		for (size_t index = 0; index + 2 < sphere.Indices.size(); index += 3)
		{
			const glm::vec3 a = sphere.Vertices[sphere.Indices[index]].Position;
			const glm::vec3 b = sphere.Vertices[sphere.Indices[index + 1]].Position;
			const glm::vec3 c = sphere.Vertices[sphere.Indices[index + 2]].Position;
			const glm::vec3 faceNormal = glm::cross(b - a, c - a);
			if (glm::dot(faceNormal, faceNormal) > 1e-12f)
			{
				Require(glm::dot(faceNormal, a + b + c) > 0.0f, "A triangle of the sphere winds clockwise seen from outside");
			}
		}
	}
}

int main()
{
	try
	{
		TestFlatFaces();
		TestSphere();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All mesh tests passed\n";
	return 0;
}
