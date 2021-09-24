﻿#pragma once
#include <gvk.hpp>

namespace gvk
{
	/** Meshlet struct for the CPU side. */
	struct meshlet
	{
		/** The model of the meshlet. */
		model mModel;
		/** The optional mesh index of the meshlet.
		 *  Only set if the submeshes were not combined upon creation of the meshlet.
		 */
		std::optional<mesh_index_t> mMeshIndex;
		/** Contains indices into the original vertex attributes of the mesh.
		 *  If submeshes were combined, it indexes the vertex attributes of the combined meshes as done with get_vertices_and_indices.
		 */
		std::vector<uint32_t> mVertices;
		/** Contains indices into the mVertices vector. */
		std::vector<uint8_t> mIndices;
		/** The actual number of vertices inside of mVertices; */
		uint32_t mVertexCount;
		/** The actual number of indices in mIndices; */
		uint32_t mIndexCount;
	};

	// ToDo: Can we make vertex and index size configurable easily?
	/** Meshlet for GPU usage */
	struct alignas(16) meshlet_gpu_data
	{
		/** Vertex indices into the vertex array */
		uint32_t mVertices[64];
		/** Indices into the vertex indices */
		uint8_t mIndices[378]; // 126*3
		/** The vertex count */
		uint8_t mVertexCount;
		/** The tirangle count */
		uint8_t mTriangleCount;
	};

	/** Meshlet for GPU usage in combination with the meshlet data generated by convert_for_gpu_usage */
	struct alignas(16) meshlet_indirect_gpu_data
	{
		/** Data offset into the meshlet data array */
		uint32_t mDataOffset;
		/** The vertex count */
		uint8_t mVertexCount;
		/** The tirangle count */
		uint8_t mTriangleCount;
	};


	/** Divides the given models into meshlets using the default implementation divide_into_meshlets_simple.
	 *  @param	aModels				All the models and associated meshes that should be divided into meshlets.
	 *								If aCombineSubmeshes is enabled, all the submeshes of a given model will be combined into a single vertex/index buffer.
	 *	@param	aMaxVertices		The maximum number of vertices of a meshlet.
	 *	@param	aMaxIndices			The maximum number of indices of a meshlet.
	 *	@param	aCombineSubmeshes	If submeshes should be combined into a single vertex/index buffer.
	 */
	std::vector<meshlet> divide_into_meshlets(const std::vector<std::tuple<avk::resource_ownership<gvk::model_t>, std::vector<mesh_index_t>>>& aModels,
		const uint32_t aMaxVertices = 64, const uint32_t aMaxIndices = 378, const bool aCombineSubmeshes = true);


	/** Divides the given models into meshlets using the given callback function.
	 *  @param	aModels				All the models and associated meshes that should be divided into meshlets.
	 *								If aCombineSubmeshes is enabled, all the submeshes of a given model will be combined into a single vertex/index buffer.
	 *  @param	aMeshletDivision	Callback used to divide meshes into meshlets.
	 *	@param	aMaxVertices		The maximum number of vertices of a meshlet.
	 *	@param	aMaxIndices			The maximum number of indices of a meshlet.
	 *	@param	aCombineSubmeshes	If submeshes should be combined into a single vertex/index buffer.
	 */
	template <typename F>
	extern std::vector<meshlet> divide_into_meshlets(const std::vector<std::tuple<avk::resource_ownership<gvk::model_t>, std::vector<mesh_index_t>>>& aModels, F aMeshletDivision,
		const uint32_t aMaxVertices = 64, const uint32_t aMaxIndices = 378, const bool aCombineSubmeshes = true)
	{
		std::vector<meshlet> meshlets;
		for (auto& pair : aModels) {
			auto& model = std::get<avk::resource_ownership<model_t>>(pair);
			auto& meshIndices = std::get<std::vector<mesh_index_t>>(pair);

			if (aCombineSubmeshes) {
				auto [vertices, indices] = get_vertices_and_indices(std::vector({ std::make_tuple(avk::const_referenced(model.get()), meshIndices) }));
				std::vector<meshlet> tmpMeshlets = divide_vertices_into_meshlets(vertices, indices, std::move(model), std::nullopt, aMaxVertices, aMaxIndices, aMeshletDivision);
				// append to meshlets
				meshlets.insert(std::end(meshlets), std::begin(tmpMeshlets), std::end(tmpMeshlets));
			}
			else {
				for (const auto meshIndex : meshIndices) {
					auto vertices = model.get().positions_for_mesh(meshIndex);
					auto indices = model.get().indices_for_mesh<uint32_t>(meshIndex);
					std::vector<meshlet> tmpMeshlets = divide_vertices_into_meshlets(vertices, indices, std::move(model), meshIndex, aMaxVertices, aMaxIndices, aMeshletDivision);
					// append to meshlets
					meshlets.insert(std::end(meshlets), std::begin(tmpMeshlets), std::end(tmpMeshlets));
				}
			}
		}
		return meshlets;
	}

	/** Divides the given vertex and index buffer into meshlets using the given callback function.
	 *  @param	aVertices			The vertex buffer.
	 *  @param	aIndices			The index buffer.
	 *  @param	aModel				The model these buffers belong to.
	 *	@param	aMeshIndex			The optional mesh index of the mesh these buffers belong to.
	 *  @param	aMeshletDivision	Callback used to divide meshes into meshlets.
	 *	@param	aMaxVertices		The maximum number of vertices of a meshlet.
	 *	@param	aMaxIndices			The maximum number of indices of a meshlet.
	 */
	template <typename F>
	extern std::vector<meshlet> divide_vertices_into_meshlets(
		const std::vector<glm::vec3>& aVertices,
		const std::vector<uint32_t>& aIndices,
		avk::resource_ownership<gvk::model_t> aModel,
		const std::optional<mesh_index_t> aMeshIndex,
		const uint32_t aMaxVertices, const uint32_t aMaxIndices,
		F aMeshletDivision)
	{
		std::vector<meshlet> tmpMeshlets;
		auto ownedModel = aModel.own();
		ownedModel.enable_shared_ownership();

		if constexpr (std::is_assignable_v<std::function<std::vector<meshlet>(const std::vector<uint32_t>&tIndices, const model_t & tModel, std::optional<mesh_index_t> tMeshIndex, uint32_t tMaxVertices, uint32_t tMaxIndices)>, decltype(aMeshletDivision)>) {
			tmpMeshlets = aMeshletDivision(aIndices, ownedModel.get(), aMeshIndex, aMaxVertices, aMaxIndices);
		}
		else if constexpr (std::is_assignable_v<std::function<std::vector<meshlet>(const std::vector<glm::vec3> &tVertices, const std::vector<uint32_t>&tIndices, const model_t & tModel, std::optional<mesh_index_t> tMeshIndex, uint32_t tMaxVertices, uint32_t tMaxIndices)>, decltype(aMeshletDivision)>) {
			tmpMeshlets = aMeshletDivision(aVertices, aIndices, ownedModel.get(), aMeshIndex, aMaxVertices, aMaxIndices);
		}
		else {
#if defined(_MSC_VER) && defined(__cplusplus)
			static_assert(false);
#else
			assert(false);
#endif
			throw avk::logic_error("No lambda has been passed to divide_into_meshlets.");
		}

		for (auto& meshlet : tmpMeshlets)
		{
			meshlet.mModel = ownedModel;
		}

		return tmpMeshlets;
	}

	/** Divides the given index buffer into meshlets using a very bad algorithm. Use something else if possible.
	 *  @param	aIndices			The index buffer.
	 *  @param	aModel				The model these buffers belong to.
	 *	@param	aMeshIndex			The optional mesh index of the mesh these buffers belong to.
	 *	@param	aMaxVertices		The maximum number of vertices of a meshlet.
	 *	@param	aMaxIndices			The maximum number of indices of a meshlet.
	 */
	std::vector<meshlet> divide_into_meshlets_simple(
		const std::vector<uint32_t>& aIndices,
		const model_t& aModel,
		std::optional<mesh_index_t> aMeshIndex,
		uint32_t aMaxVertices, uint32_t aMaxIndices);

	/** Converts meshlets into a GPU usable representation.
	 *	@param	meshlets	The meshlets to convert
	 *	@tparam	T			Either meshlet_gpu_data or meshlet_indirect_gpu_data. If the indirect representation is used, the meshlet data will also be returned.
	 *						The meshlet data contains the vertex indices from [mDataOffset] to [mDataOffset + mVertexCount].
	 *						It also contains the indices into the vertex indices, four uint8 packed into a single uint32,
	 *						from [mDataOffset + mVertexCount] to [mDataOffset + mVertexCount + (mIndexCount+3)/4]
	 * @returns				A Tuple of the converted meshlets into the provided type and the optional meshlet data when the indirect representation
	 *						is used.
	 */
	template <typename T>
	std::tuple<std::vector<T>, std::optional<std::vector<uint32_t>>> convert_for_gpu_usage(const std::vector<meshlet>& meshlets)
	{
		std::vector<T> gpuMeshlets;
		std::optional<std::vector<uint32_t>> vertexIndices = std::nullopt;
		gpuMeshlets.reserve(meshlets.size());
		for (auto& meshlet : meshlets) {
			auto& newEntry = gpuMeshlets.emplace_back();
			if constexpr (std::is_convertible_v<T, meshlet_gpu_data>) {
				auto& ml = static_cast<meshlet_gpu_data&>(newEntry);
				ml.mVertexCount = meshlet.mVertexCount;
				ml.mTriangleCount = meshlet.mIndexCount / 3u;
				std::ranges::copy(meshlet.mVertices, ml.mVertices);
				std::ranges::copy(meshlet.mIndices, ml.mIndices);
			}
			else if constexpr (std::is_convertible_v<T, meshlet_indirect_gpu_data>) {
				if (!vertexIndices.has_value()) {
					vertexIndices = std::vector<uint32_t>();
				}
				auto& ml = static_cast<meshlet_indirect_gpu_data&>(newEntry);
				ml.mVertexCount = meshlet.mVertexCount;
				ml.mTriangleCount = meshlet.mIndexCount / 3u;
				// copy vertex indices
				vertexIndices->insert(vertexIndices->end(), meshlet.mVertices.begin(), meshlet.mVertices.end());
				// ToDo: can you do that with ranges somehow, like above? problem is that 4 indices need to be packed in a single integer
				// pack indices
				const uint32_t* indexGroups = reinterpret_cast<const uint32_t*>(meshlet.mIndices.data());
				const uint32_t indexGroupCount = (meshlet.mIndexCount + 3) / 4;
				for (int i = 0; i < indexGroupCount; i++)
				{
					vertexIndices->push_back(indexGroups[i]);
				}
			}
		}
		return std::forward_as_tuple(gpuMeshlets, vertexIndices);
	}
}
