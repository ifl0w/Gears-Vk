#include <gvk.hpp>
#include <imgui.h>
#include "../shaders/cpu_gpu_shared_config.h"

#define USE_CACHE 1

static constexpr size_t sNumVertices = 64;
static constexpr size_t sNumIndices = 378;
static constexpr uint32_t cConcurrentFrames = 3u;

class skinned_meshlets_app : public gvk::invokee
{
	struct alignas(16) push_constants
	{
		VkBool32 mHighlightMeshlets;
	};

	/** Contains the necessary buffers for drawing everything */
	struct data_for_draw_call
	{
		avk::buffer mPositionsBuffer;
		avk::buffer mTexCoordsBuffer;
		avk::buffer mNormalsBuffer;
		avk::buffer mBoneIndicesBuffer;
		avk::buffer mBoneWeightsBuffer;
#if USE_REDIRECTED_GPU_DATA
		avk::buffer mMeshletDataBuffer;
#endif

		glm::mat4 mModelMatrix;

		uint32_t mMaterialIndex;
		uint32_t mModelIndex;
	};

	/** Contains the data for each draw call */
	struct loaded_data_for_draw_call
	{
		std::vector<glm::vec3> mPositions;
		std::vector<glm::vec2> mTexCoords;
		std::vector<glm::vec3> mNormals;
		std::vector<uint32_t> mIndices;
		std::vector<glm::uvec4> mBoneIndices;
		std::vector<glm::vec4> mBoneWeights;
#if USE_REDIRECTED_GPU_DATA
		std::vector<uint32_t> mMeshletData;
#endif

		glm::mat4 mModelMatrix;

		uint32_t mMaterialIndex;
		uint32_t mModelIndex;
	};

	struct additional_animated_model_data
	{
		std::vector<glm::mat4> mBoneMatricesAni;
	};

	/** Helper struct for the animations. */
	struct animated_model_data
	{
		animated_model_data() = default;
		animated_model_data(animated_model_data&&) = default;
		animated_model_data& operator=(animated_model_data&&) = default;
		~animated_model_data() = default;
		// prevent copying of the data:
		animated_model_data(const animated_model_data&) = delete;
		animated_model_data& operator=(const animated_model_data&) = delete;

		std::string mModelName;
		gvk::animation_clip_data mClip;
		uint32_t mNumBoneMatrices;
		size_t mBoneMatricesBufferIndex;
		gvk::animation mAnimation;

		[[nodiscard]] double start_sec() const { return mClip.mStartTicks / mClip.mTicksPerSecond; }
		[[nodiscard]] double end_sec() const { return mClip.mEndTicks / mClip.mTicksPerSecond; }
		[[nodiscard]] double duration_sec() const { return end_sec() - start_sec(); }
		[[nodiscard]] double duration_ticks() const { return mClip.mEndTicks - mClip.mStartTicks; }
	};

	/** The meshlet we upload to the gpu with its additional data. */
	struct alignas(16) meshlet
	{
		glm::mat4 mTransformationMatrix;
		uint32_t mMaterialIndex;
		uint32_t mTexelBufferIndex;
		uint32_t mModelIndex;

#if !USE_REDIRECTED_GPU_DATA
		gvk::meshlet_gpu_data<sNumVertices, sNumIndices> mGeometry;
#else
		gvk::meshlet_redirected_gpu_data mGeometry;
#endif
	};

public: // v== avk::invokee overrides which will be invoked by the framework ==v
	skinned_meshlets_app(avk::queue& aQueue)
		: mQueue{ &aQueue }
	{}

	/** Creates buffers for all the drawcalls.
	 *  Called after everything has been loaded and split into meshlets properly.
	 *  @param dataForDrawCall		The loaded data for the drawcalls.
	 *	@param drawCallsTarget		The target vector for the draw call data.
	 */
	void add_draw_calls(std::vector<loaded_data_for_draw_call>& dataForDrawCall, std::vector<data_for_draw_call>& drawCallsTarget) {
		for (auto& drawCallData : dataForDrawCall) {
			auto& drawCall = drawCallsTarget.emplace_back();
			drawCall.mModelMatrix = drawCallData.mModelMatrix;
			drawCall.mMaterialIndex = drawCallData.mMaterialIndex;

			drawCall.mPositionsBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mPositions).describe_only_member(drawCallData.mPositions[0], avk::content_description::position),
				avk::storage_buffer_meta::create_from_data(drawCallData.mPositions),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mPositions).describe_only_member(drawCallData.mPositions[0]) // just take the vec3 as it is
			);

			drawCall.mNormalsBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mNormals),
				avk::storage_buffer_meta::create_from_data(drawCallData.mNormals),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mNormals).describe_only_member(drawCallData.mNormals[0]) // just take the vec3 as it is
			);

			drawCall.mTexCoordsBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mTexCoords),
				avk::storage_buffer_meta::create_from_data(drawCallData.mTexCoords),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mTexCoords).describe_only_member(drawCallData.mTexCoords[0]) // just take the vec2 as it is   
			);

#if USE_REDIRECTED_GPU_DATA
			drawCall.mMeshletDataBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mMeshletData),
				avk::storage_buffer_meta::create_from_data(drawCallData.mMeshletData),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mMeshletData).describe_only_member(drawCallData.mMeshletData[0])
			);
#endif

			drawCall.mBoneIndicesBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mBoneIndices),
				avk::storage_buffer_meta::create_from_data(drawCallData.mBoneIndices),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mBoneIndices).describe_only_member(drawCallData.mBoneIndices[0]) // just take the uvec4 as it is 
			);

			drawCall.mBoneWeightsBuffer = gvk::context().create_buffer(avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(drawCallData.mBoneWeights),
				avk::storage_buffer_meta::create_from_data(drawCallData.mBoneWeights),
				avk::uniform_texel_buffer_meta::create_from_data(drawCallData.mBoneWeights).describe_only_member(drawCallData.mBoneWeights[0]) // just take the vec4 as it is 
			);

			drawCall.mPositionsBuffer->fill(drawCallData.mPositions.data(), 0, avk::sync::wait_idle(true));
			drawCall.mNormalsBuffer->fill(drawCallData.mNormals.data(), 0, avk::sync::wait_idle(true));
			drawCall.mTexCoordsBuffer->fill(drawCallData.mTexCoords.data(), 0, avk::sync::wait_idle(true));
#if USE_REDIRECTED_GPU_DATA
			drawCall.mMeshletDataBuffer->fill(drawCallData.mMeshletData.data(), 0, avk::sync::wait_idle(true));
#endif
			drawCall.mBoneIndicesBuffer->fill(drawCallData.mBoneIndices.data(), 0, avk::sync::wait_idle(true));
			drawCall.mBoneWeightsBuffer->fill(drawCallData.mBoneWeights.data(), 0, avk::sync::wait_idle(true));

			// add them to the texel buffers
			mPositionBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mPositionsBuffer)));
			mNormalBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mNormalsBuffer)));
			mTexCoordsBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mTexCoordsBuffer)));
#if USE_REDIRECTED_GPU_DATA
			mMeshletDataBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mMeshletDataBuffer)));
#endif
			mBoneIndicesBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mBoneIndicesBuffer)));
			mBoneWeightsBuffers.push_back(gvk::context().create_buffer_view(shared(drawCall.mBoneWeightsBuffer)));
		}
	}

	void initialize() override
	{
		// use helper functions to create ImGui elements
		auto surfaceCap = gvk::context().physical_device().getSurfaceCapabilitiesKHR(gvk::context().main_window()->surface());

		// Create a descriptor cache that helps us to conveniently create descriptor sets:
		mDescriptorCache = gvk::context().create_descriptor_cache();

		glm::mat4 globalTransform = glm::rotate(glm::radians(180.f), glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(1.f));
		std::vector<gvk::model> loadedModels;
		// Load a model from file:
		auto model = gvk::model_t::load_from_file("assets/crab.fbx", aiProcess_Triangulate);

		loadedModels.push_back(std::move(model));

		std::vector<gvk::material_config> allMatConfigs; // <-- Gather the material config from all models to be loaded
		std::vector<loaded_data_for_draw_call> dataForDrawCall;
		std::vector<meshlet> meshletsGeometry;
		std::vector<animated_model_data> animatedModels;

		// Crab-specific animation config: (Needs to be adapted for other models)
		const uint32_t cAnimationIndex = 0;
		const uint32_t cStartTimeTicks = 0;
		const uint32_t cEndTimeTicks   = 58;
		const uint32_t cTicksPerSecond = 34;

		// Generate the meshlets for each loaded model.
		for (size_t i = 0; i < loadedModels.size(); ++i) {
			auto curModel = std::move(loadedModels[i]);

			// load the animation
			auto curClip = curModel->load_animation_clip(cAnimationIndex, cStartTimeTicks, cEndTimeTicks);
			curClip.mTicksPerSecond = cTicksPerSecond;
			auto& curEntry = animatedModels.emplace_back();
			curEntry.mModelName = curModel->path();
			curEntry.mClip = curClip;

			// get all the meshlet indices of the model
			const auto meshIndicesInOrder = curModel->select_all_meshes();

			curEntry.mNumBoneMatrices = curModel->num_bone_matrices(meshIndicesInOrder);

			// Store offset into the vector of buffers that store the bone matrices
			curEntry.mBoneMatricesBufferIndex = i;

			auto distinctMaterials = curModel->distinct_material_configs();
			const auto matOffset = allMatConfigs.size();
			// add all the materials of the model
			for (auto& pair : distinctMaterials) {
				allMatConfigs.push_back(pair.first);
			}

			// prepare the animation for the current entry
			curEntry.mAnimation = curModel->prepare_animation(curEntry.mClip.mAnimationIndex, meshIndicesInOrder);

			// Generate meshlets for each submesh of the current loaded model. Load all it's data into the drawcall for later use.
			for (size_t mpos = 0; mpos < meshIndicesInOrder.size(); mpos++) {
				auto meshIndex = meshIndicesInOrder[mpos];
				std::string meshname = curModel->name_of_mesh(mpos);

				auto texelBufferIndex = dataForDrawCall.size();
				auto& drawCallData = dataForDrawCall.emplace_back();

				drawCallData.mMaterialIndex = static_cast<int32_t>(matOffset);
				drawCallData.mModelMatrix = globalTransform;
				drawCallData.mModelIndex = static_cast<uint32_t>(curEntry.mBoneMatricesBufferIndex);
				// Find and assign the correct material (in the ~"global" allMatConfigs vector!)
				for (auto pair : distinctMaterials) {
					if (std::end(pair.second) != std::find(std::begin(pair.second), std::end(pair.second), meshIndex)) {
						break;
					}

					drawCallData.mMaterialIndex++;
				}

				auto selection = gvk::make_models_and_meshes_selection(curModel, meshIndex);
				std::vector<gvk::mesh_index_t> meshIndices;
				meshIndices.push_back(meshIndex);
				// Build meshlets:
				std::tie(drawCallData.mPositions, drawCallData.mIndices) = gvk::get_vertices_and_indices(selection);
				drawCallData.mNormals = gvk::get_normals(selection);
				drawCallData.mTexCoords = gvk::get_2d_texture_coordinates(selection, 0);
				// Get bone indices and weights too
				drawCallData.mBoneIndices = gvk::get_bone_indices_for_single_target_buffer(selection, meshIndicesInOrder);
				drawCallData.mBoneWeights = gvk::get_bone_weights(selection);

				// create selection for the meshlets
				auto meshletSelection = gvk::make_selection_of_shared_models_and_mesh_indices(curModel, meshIndex);

				auto cpuMeshlets = gvk::divide_into_meshlets(meshletSelection);
#if !USE_REDIRECTED_GPU_DATA
#if USE_CACHE
				gvk::serializer serializer("direct_meshlets-" + meshname + "-" + std::to_string(mpos) + ".cache");
				auto [gpuMeshlets, _] = gvk::convert_for_gpu_usage_cached<gvk::meshlet_gpu_data<sNumVertices, sNumIndices>>(serializer, cpuMeshlets);
#else
				auto [gpuMeshlets, _] = gvk::convert_for_gpu_usage<gvk::meshlet_gpu_data<sNumVertices, sNumIndices>, sNumVertices, sNumIndices>(cpuMeshlets);
#endif
#else
#if USE_CACHE
				gvk::serializer serializer("indirect_meshlets-" + meshname + "-" + std::to_string(mpos) + ".cache");
				auto [gpuMeshlets, generatedMeshletData] = gvk::convert_for_gpu_usage_cached<gvk::meshlet_redirected_gpu_data, sNumVertices, sNumIndices>(serializer, cpuMeshlets);
#else
				auto [gpuMeshlets, generatedMeshletData] = gvk::convert_for_gpu_usage<gvk::meshlet_redirected_gpu_data, sNumVertices, sNumIndices>(cpuMeshlets);
#endif
				drawCallData.mMeshletData = std::move(generatedMeshletData.value());
#endif

				// fill our own meshlets with the loaded/generated data
				for (size_t mshltidx = 0; mshltidx < gpuMeshlets.size(); ++mshltidx) {
					auto& genMeshlet = gpuMeshlets[mshltidx];

					auto& ml = meshletsGeometry.emplace_back(meshlet{});

#pragma region start to assemble meshlet struct
					ml.mTransformationMatrix = drawCallData.mModelMatrix;
					ml.mMaterialIndex = drawCallData.mMaterialIndex;
					ml.mTexelBufferIndex = static_cast<uint32_t>(texelBufferIndex);
					ml.mModelIndex = static_cast<uint32_t>(curEntry.mBoneMatricesBufferIndex);

					ml.mGeometry = genMeshlet;
#pragma endregion 
				}
			}
		} // for (size_t i = 0; i < loadedModels.size(); ++i)

		// create buffers for animation data
		for (size_t i = 0; i < loadedModels.size(); ++i) {
			auto& animModel = mAnimatedModels.emplace_back(std::move(animatedModels[i]), additional_animated_model_data{});

			// buffers for the animated bone matrices, will be populated before rendering
			std::get<additional_animated_model_data>(animModel).mBoneMatricesAni.resize(std::get<animated_model_data>(animModel).mNumBoneMatrices);
			for (size_t cfi = 0; cfi < cConcurrentFrames; ++cfi) {
				mBoneMatricesBuffersAni[cfi].push_back(gvk::context().create_buffer(
					avk::memory_usage::host_coherent, {},
					avk::storage_buffer_meta::create_from_data(std::get<additional_animated_model_data>(animModel).mBoneMatricesAni)
				));
			}
		}
		// create all the buffers for our drawcall data
		add_draw_calls(dataForDrawCall, mDrawCalls);

		// Put the meshlets that we have gathered into a buffer:
		mMeshletsBuffer = gvk::context().create_buffer(
			avk::memory_usage::device, {},
			avk::storage_buffer_meta::create_from_data(meshletsGeometry)
		);
		mMeshletsBuffer->fill(meshletsGeometry.data(), 0, avk::sync::wait_idle(true));
		mNumMeshletWorkgroups = meshletsGeometry.size();

		// For all the different materials, transfer them in structs which are well
		// suited for GPU-usage (proper alignment, and containing only the relevant data),
		// also load all the referenced images from file and provide access to them
		// via samplers; It all happens in `ak::convert_for_gpu_usage`:
		auto [gpuMaterials, imageSamplers] = gvk::convert_for_gpu_usage<gvk::material_gpu_data>(
			allMatConfigs, false, true,
			avk::image_usage::general_texture,
			avk::filter_mode::trilinear,
			avk::sync::with_barriers(gvk::context().main_window()->command_buffer_lifetime_handler())
			);

		mViewProjBuffer = gvk::context().create_buffer(
			avk::memory_usage::host_visible, {},
			avk::uniform_buffer_meta::create_from_data(glm::mat4())
		);

		mMaterialBuffer = gvk::context().create_buffer(
			avk::memory_usage::host_visible, {},
			avk::storage_buffer_meta::create_from_data(gpuMaterials)
		);
		mMaterialBuffer->fill(
			gpuMaterials.data(), 0,
			avk::sync::not_required()
		);

		mImageSamplers = std::move(imageSamplers);

		auto swapChainFormat = gvk::context().main_window()->swap_chain_image_format();
		// Create our rasterization graphics pipeline with the required configuration:
		mPipeline = gvk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::task_shader("shaders/meshlet.task"),
			avk::mesh_shader("shaders/meshlet.mesh"),
			avk::fragment_shader("shaders/diffuse_shading_fixed_lightsource.frag"),
			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_counter_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(gvk::context().main_window()->backbuffer_at_index(0)),
			// We'll render to the back buffer, which has a color attachment always, and in our case additionally a depth
			// attachment, which has been configured when creating the window. See main() function!
			avk::attachment::declare(gvk::format_from_window_color_buffer(gvk::context().main_window()), avk::on_load::clear, avk::color(0), avk::on_store::store),	 // But not in presentable format, because ImGui comes after
			avk::attachment::declare(gvk::format_from_window_depth_buffer(gvk::context().main_window()), avk::on_load::clear, avk::depth_stencil(), avk::on_store::dont_care),
			// The following define additional data which we'll pass to the pipeline:
			avk::push_constant_binding_data{ avk::shader_type::fragment, 0, sizeof(push_constants) },
			avk::descriptor_binding(0, 0, mImageSamplers),
			avk::descriptor_binding(0, 1, mViewProjBuffer),
			avk::descriptor_binding(1, 0, mMaterialBuffer),
			avk::descriptor_binding(2, 0, mBoneMatricesBuffersAni[0]),
			// texel buffers
			avk::descriptor_binding(3, 0, avk::as_uniform_texel_buffer_views(mPositionBuffers)),
			avk::descriptor_binding(3, 2, avk::as_uniform_texel_buffer_views(mNormalBuffers)),
			avk::descriptor_binding(3, 3, avk::as_uniform_texel_buffer_views(mTexCoordsBuffers)),
#if USE_REDIRECTED_GPU_DATA
			avk::descriptor_binding(3, 4, avk::as_uniform_texel_buffer_views(mMeshletDataBuffers)),
#endif
			avk::descriptor_binding(3, 5, avk::as_uniform_texel_buffer_views(mBoneIndicesBuffers)),
			avk::descriptor_binding(3, 6, avk::as_uniform_texel_buffer_views(mBoneWeightsBuffers)),
			avk::descriptor_binding(4, 0, mMeshletsBuffer)
		);

		// set up updater
		// we want to use an updater, so create one:

		mUpdater.emplace();
		mPipeline.enable_shared_ownership(); // Make it usable with the updater
		mUpdater->on(gvk::shader_files_changed_event(mPipeline)).update(mPipeline);
		mPipeline.enable_shared_ownership(); // Make it usable with the updater

		mUpdater->on(gvk::swapchain_resized_event(gvk::context().main_window())).invoke([this]() {
			this->mQuakeCam.set_aspect_ratio(gvk::context().main_window()->aspect_ratio());
			});

		//first make sure render pass is updated
		mUpdater->on(gvk::swapchain_format_changed_event(gvk::context().main_window()),
			gvk::swapchain_additional_attachments_changed_event(gvk::context().main_window())
		).invoke([this]() {
			std::vector<avk::attachment> renderpassAttachments = {
				avk::attachment::declare(gvk::format_from_window_color_buffer(gvk::context().main_window()), avk::on_load::clear, avk::color(0),		avk::on_store::store),	 // But not in presentable format, because ImGui comes after
			};
			auto renderPass = gvk::context().create_renderpass(renderpassAttachments);
			gvk::context().replace_render_pass_for_pipeline(mPipeline, std::move(renderPass));
			}).then_on( // ... next, at this point, we are sure that the render pass is correct -> check if there are events that would update the pipeline
				gvk::swapchain_changed_event(gvk::context().main_window()),
				gvk::shader_files_changed_event(mPipeline)
			).update(mPipeline);

		// Add the camera to the composition (and let it handle the updates)
		mQuakeCam.set_translation({ 0.0f, -1.0f, 8.0f });
		mQuakeCam.set_perspective_projection(glm::radians(60.0f), gvk::context().main_window()->aspect_ratio(), 0.3f, 1000.0f);
		//mQuakeCam.set_orthographic_projection(-5, 5, -5, 5, 0.5, 100);
		gvk::current_composition()->add_element(mQuakeCam);

		auto imguiManager = gvk::current_composition()->element_by_type<gvk::imgui_manager>();
		if (nullptr != imguiManager) {
			imguiManager->add_callback([this]() {
				ImGui::Begin("Info & Settings");
				ImGui::SetWindowPos(ImVec2(1.0f, 1.0f), ImGuiCond_FirstUseEver);
				ImGui::Text("%.3f ms/frame", 1000.0f / ImGui::GetIO().Framerate);
				ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
				ImGui::TextColored(ImVec4(0.f, .6f, .8f, 1.f), "[F1]: Toggle input-mode");
				ImGui::TextColored(ImVec4(0.f, .6f, .8f, 1.f), " (UI vs. scene navigation)");

				ImGui::Checkbox("Highlight Meshlets", &mHighlightMeshlets);

				ImGui::End();
			});
		}
	}

	void update() override
	{
		if (gvk::input().key_pressed(gvk::key_code::c)) {
			// Center the cursor:
			auto resolution = gvk::context().main_window()->resolution();
			gvk::context().main_window()->set_cursor_pos({ resolution[0] / 2.0, resolution[1] / 2.0 });
		}
		if (gvk::input().key_pressed(gvk::key_code::escape)) {
			// Stop the current composition:
			gvk::current_composition()->stop();
		}
		if (gvk::input().key_pressed(gvk::key_code::left)) {
			mQuakeCam.look_along(gvk::left());
		}
		if (gvk::input().key_pressed(gvk::key_code::right)) {
			mQuakeCam.look_along(gvk::right());
		}
		if (gvk::input().key_pressed(gvk::key_code::up)) {
			mQuakeCam.look_along(gvk::front());
		}
		if (gvk::input().key_pressed(gvk::key_code::down)) {
			mQuakeCam.look_along(gvk::back());
		}
		if (gvk::input().key_pressed(gvk::key_code::page_up)) {
			mQuakeCam.look_along(gvk::up());
		}
		if (gvk::input().key_pressed(gvk::key_code::page_down)) {
			mQuakeCam.look_along(gvk::down());
		}
		if (gvk::input().key_pressed(gvk::key_code::home)) {
			mQuakeCam.look_at(glm::vec3{ 0.0f, 0.0f, 0.0f });
		}

		if (gvk::input().key_pressed(gvk::key_code::f1)) {
			auto imguiManager = gvk::current_composition()->element_by_type<gvk::imgui_manager>();
			if (mQuakeCam.is_enabled()) {
				mQuakeCam.disable();
				if (nullptr != imguiManager) { imguiManager->enable_user_interaction(true); }
			}
			else {
				mQuakeCam.enable();
				if (nullptr != imguiManager) { imguiManager->enable_user_interaction(false); }
			}
		}
	}

	void render() override
	{
		auto mainWnd = gvk::context().main_window();
		auto ifi = mainWnd->current_in_flight_index();

		// Animate all the meshes
		for (auto& model : mAnimatedModels) {
			auto& animation = std::get<animated_model_data>(model).mAnimation;
			auto& clip = std::get<animated_model_data>(model).mClip;
			const auto doubleTime = fmod(gvk::time().absolute_time_dp(), std::get<animated_model_data>(model).duration_sec() * 2);
			auto time = glm::lerp(std::get<animated_model_data>(model).start_sec(), std::get<animated_model_data>(model).end_sec(), (doubleTime > std::get<animated_model_data>(model).duration_sec() ? doubleTime - std::get<animated_model_data>(model).duration_sec() : doubleTime) / std::get<animated_model_data>(model).duration_sec());
			auto targetMemory = std::get<additional_animated_model_data>(model).mBoneMatricesAni.data();

			// Use lambda option 1 that takes as parameters: mesh_bone_info, inverse mesh root matrix, global node/bone transform w.r.t. the animation, inverse bind-pose matrix
			animation.animate(clip, time, [this, &animation, targetMemory](gvk::mesh_bone_info aInfo, const glm::mat4& aInverseMeshRootMatrix, const glm::mat4& aTransformMatrix, const glm::mat4& aInverseBindPoseMatrix, const glm::mat4& aLocalTransformMatrix, size_t aAnimatedNodeIndex, size_t aBoneMeshTargetIndex, double aAnimationTimeInTicks) {
				// Construction of the bone matrix for this node:
				//   1. Bring vertex into bone space
				//   2. Apply transformaton in bone space => MODEL SPACE
				targetMemory[aInfo.mGlobalBoneIndexOffset + aInfo.mMeshLocalBoneIndex] = aTransformMatrix * aInverseBindPoseMatrix;
			});
		}

		// Upload the updated bone matrices into the buffer for the current frame (considering that we have cConcurrentFrames-many concurrent frames):
		for (auto& models : mAnimatedModels) {
			mBoneMatricesBuffersAni[ifi][std::get<animated_model_data>(models).mBoneMatricesBufferIndex]->fill(std::get<additional_animated_model_data>(models).mBoneMatricesAni.data(), 0, avk::sync::not_required());
		}

		auto viewProjMat = mQuakeCam.projection_matrix() * mQuakeCam.view_matrix();
		mViewProjBuffer->fill(glm::value_ptr(viewProjMat), 0, avk::sync::not_required());

		auto pushConstants = push_constants{ mHighlightMeshlets };

		auto& commandPool = gvk::context().get_command_pool_for_single_use_command_buffers(*mQueue);
		auto cmdbfr = commandPool->alloc_command_buffer(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
		cmdbfr->begin_recording();
		cmdbfr->begin_render_pass_for_framebuffer(mPipeline->get_renderpass(), gvk::context().main_window()->current_backbuffer());
		cmdbfr->bind_pipeline(avk::const_referenced(mPipeline));
		cmdbfr->bind_descriptors(mPipeline->layout(), mDescriptorCache.get_or_create_descriptor_sets({
			avk::descriptor_binding(0, 0, mImageSamplers),
			avk::descriptor_binding(0, 1, mViewProjBuffer),
			avk::descriptor_binding(1, 0, mMaterialBuffer),
			avk::descriptor_binding(2, 0, mBoneMatricesBuffersAni[ifi]),
			avk::descriptor_binding(3, 0, avk::as_uniform_texel_buffer_views(mPositionBuffers)),
			avk::descriptor_binding(3, 2, avk::as_uniform_texel_buffer_views(mNormalBuffers)),
			avk::descriptor_binding(3, 3, avk::as_uniform_texel_buffer_views(mTexCoordsBuffers)),
#if USE_REDIRECTED_GPU_DATA
					avk::descriptor_binding(3, 4, avk::as_uniform_texel_buffer_views(mMeshletDataBuffers)),
#endif
			avk::descriptor_binding(3, 5, avk::as_uniform_texel_buffer_views(mBoneIndicesBuffers)),
			avk::descriptor_binding(3, 6, avk::as_uniform_texel_buffer_views(mBoneWeightsBuffers)),
			avk::descriptor_binding(4, 0, mMeshletsBuffer)
			}));
		cmdbfr->handle().pushConstants(mPipeline->layout_handle(), vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &pushConstants);
		// draw our meshlets
		cmdbfr->handle().drawMeshTasksNV(mNumMeshletWorkgroups, 0);

		cmdbfr->end_render_pass();
		cmdbfr->end_recording();

		// The swap chain provides us with an "image available semaphore" for the current frame.
		// Only after the swapchain image has become available, we may start rendering into it.
		auto imageAvailableSemaphore = mainWnd->consume_current_image_available_semaphore();

		// Submit the draw call and take care of the command buffer's lifetime:
		mQueue->submit(cmdbfr, imageAvailableSemaphore);
		mainWnd->handle_lifetime(avk::owned(cmdbfr));
	}

private: // v== Member variables ==v

	avk::queue* mQueue;
	avk::descriptor_cache mDescriptorCache;

	std::vector<std::tuple<animated_model_data, additional_animated_model_data>> mAnimatedModels;

	avk::buffer mViewProjBuffer;
	avk::buffer mMaterialBuffer;
	avk::buffer mMeshletsBuffer;
	std::array<std::vector<avk::buffer>, cConcurrentFrames> mBoneMatricesBuffersAni;
	std::vector<avk::image_sampler> mImageSamplers;

	std::vector<data_for_draw_call> mDrawCalls;
	avk::graphics_pipeline mPipeline;
	gvk::quake_camera mQuakeCam;
	size_t mNumMeshletWorkgroups;

	std::vector<avk::buffer_view> mPositionBuffers;
	std::vector<avk::buffer_view> mTexCoordsBuffers;
	std::vector<avk::buffer_view> mNormalBuffers;
	std::vector<avk::buffer_view> mBoneWeightsBuffers;
	std::vector<avk::buffer_view> mBoneIndicesBuffers;
#if USE_REDIRECTED_GPU_DATA
	std::vector<avk::buffer_view> mMeshletDataBuffers;
#endif

	bool mHighlightMeshlets;

}; // skinned_meshlets_app

int main() // <== Starting point ==
{
	try {
		// Create a window and open it
		auto mainWnd = gvk::context().create_window("Skinned Meshlets");

		mainWnd->set_resolution({ 1920, 1080 });
		mainWnd->enable_resizing(true);
		mainWnd->set_additional_back_buffer_attachments({
			avk::attachment::declare(vk::Format::eD32Sfloat, avk::on_load::clear, avk::depth_stencil(), avk::on_store::dont_care)
			});
		mainWnd->set_presentaton_mode(gvk::presentation_mode::mailbox);
		mainWnd->set_number_of_concurrent_frames(cConcurrentFrames);
		mainWnd->open();

		auto& singleQueue = gvk::context().create_queue({}, avk::queue_selection_preference::versatile_queue, mainWnd);
		mainWnd->add_queue_family_ownership(singleQueue);
		mainWnd->set_present_queue(singleQueue);

		// Create an instance of our main avk::element which contains all the functionality:
		auto app = skinned_meshlets_app(singleQueue);
		// Create another element for drawing the UI with ImGui
		auto ui = gvk::imgui_manager(singleQueue);

		// GO:
		gvk::start(
			gvk::application_name("Gears-Vk + Auto-Vk Example: Skinned Meshlets"),
			gvk::required_device_extensions(VK_NV_MESH_SHADER_EXTENSION_NAME)
			.add_extension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME),
			[](vk::PhysicalDeviceVulkan12Features& features) {
				features.setUniformAndStorageBuffer8BitAccess(VK_TRUE);
				features.setStorageBuffer8BitAccess(VK_TRUE);
			},
			mainWnd,
				app,
				ui
				);
	}
	catch (gvk::logic_error&) {}
	catch (gvk::runtime_error&) {}
	catch (avk::logic_error&) {}
	catch (avk::runtime_error&) {}
}
