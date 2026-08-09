#include <Modules/AdModuleFullPrecisionDecals.h>
#include <AdUtils.h>

#include <RE/Fallout.h>

#include <mutex>
#include <unordered_map>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesFullPrecisionDecals{ "Fixes"sv, "bFullPrecisionDecals"sv, true };
	static REX::TOML::Bool<> bAdditionalFullPrecisionDecalsMembrane{ "Additional"sv, "bFullPrecisionDecalsMembrane"sv, true };
	static REX::TOML::Bool<> bAdditionalFullPrecisionDecalsEffectShaders{ "Additional"sv, "bFullPrecisionDecalsEffectShaders"sv, true };

	namespace fullPrecisionDecalsDetail
	{
		using VertexDesc = RE::BSGraphics::VertexDesc;
		using Vertex = RE::BSGraphics::Vertex;
		using TriShapeDataAccess = void;

		template <class F>
		class ScopeExit
		{
		public:
			explicit ScopeExit(F a_function) :
				function(std::move(a_function))
			{}

			ScopeExit(const ScopeExit&) = delete;
			ScopeExit& operator=(const ScopeExit&) = delete;

			~ScopeExit()
			{
				if (active)
					function();
			}

			void Release() noexcept
			{
				active = false;
			}

		private:
			F function;
			bool active{ true };
		};

		namespace Utils
		{
			using CalculateBoneMatrices_t = void (*)(RE::BSGeometry*);

			// CalculateBoneMatrices: OG 795227 -> RVA 0x1D23A20, NG 2277105 -> 0x1712E60, AE 2277105 -> 0x182DCD0.
			static REL::Relocation<CalculateBoneMatrices_t> calculateBoneMatrices{ REL::ID{ 795227, 2277105 } };

			static void SetAttributeOffsetRaw(std::uint64_t& a_desc, Vertex::Attribute a_attribute, std::uint32_t a_offset)
			{
				if (a_attribute == Vertex::VA_POSITION)
					return;

				const auto shift = 4 * static_cast<std::uint8_t>(a_attribute) + 2;
				a_desc = (a_desc & ~(0x3CULL << shift)) | ((static_cast<std::uint64_t>(a_offset) & 0x3C) << shift);
			}

			[[nodiscard]] static std::uint16_t FloatToHalf(float a_value)
			{
				std::uint32_t bits;
				std::memcpy(std::addressof(bits), std::addressof(a_value), sizeof(bits));

				const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
				auto exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF);
				auto mantissa = bits & 0x7FFFFF;

				if (exponent == 0xFF)
					return static_cast<std::uint16_t>(sign | (mantissa != 0 ? 0x7E00 : 0x7C00));

				exponent = exponent - 127 + 15;
				if (exponent >= 0x1F)
					return static_cast<std::uint16_t>(sign | 0x7C00);

				if (exponent <= 0)
				{
					if (exponent < -10)
						return sign;

					mantissa |= 0x800000;
					const auto shift = static_cast<std::uint32_t>(14 - exponent);
					auto halfMantissa = static_cast<std::uint16_t>(mantissa >> shift);
					if ((mantissa >> (shift - 1)) & 1)
						++halfMantissa;

					return static_cast<std::uint16_t>(sign | halfMantissa);
				}

				auto half = static_cast<std::uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
				if (mantissa & 0x1000)
					++half;

				return half;
			}

			[[nodiscard]] static float HalfToFloat(std::uint16_t a_value)
			{
				const auto sign = static_cast<std::uint32_t>(a_value & 0x8000) << 16;
				auto exponent = static_cast<std::uint32_t>((a_value >> 10) & 0x1F);
				auto mantissa = static_cast<std::uint32_t>(a_value & 0x03FF);

				std::uint32_t bits = 0;
				if (exponent == 0)
				{
					if (mantissa == 0)
					{
						bits = sign;
					}
					else
					{
						exponent = 1;
						while ((mantissa & 0x0400) == 0)
						{
							mantissa <<= 1;
							--exponent;
						}
						mantissa &= 0x03FF;
						bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
					}
				}
				else if (exponent == 0x1F)
				{
					bits = sign | 0x7F800000 | (mantissa << 13);
				}
				else
				{
					bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
				}

				float result;
				std::memcpy(std::addressof(result), std::addressof(bits), sizeof(result));
				return result;
			}

			[[nodiscard]] static float DecodeSnorm8(std::uint8_t a_value)
			{
				return (static_cast<float>(a_value) / 127.5F) - 1.0F;
			}

			[[nodiscard]] static float Dot4(const float* a_row, const float* a_vector)
			{
				return (a_row[0] * a_vector[0]) + (a_row[1] * a_vector[1]) + (a_row[2] * a_vector[2]) + (a_row[3] * a_vector[3]);
			}

			[[nodiscard]] static float Dot3(const float* a_row, const float* a_vector)
			{
				return (a_row[0] * a_vector[0]) + (a_row[1] * a_vector[1]) + (a_row[2] * a_vector[2]);
			}

			static void Normalize3(float* a_vector)
			{
				const auto lengthSquared =
					(a_vector[0] * a_vector[0]) +
					(a_vector[1] * a_vector[1]) +
					(a_vector[2] * a_vector[2]);
				if (lengthSquared <= 0.0F)
				{
					a_vector[0] = 0.0F;
					a_vector[1] = 0.0F;
					a_vector[2] = 1.0F;
					return;
				}

				const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
				a_vector[0] *= inverseLength;
				a_vector[1] *= inverseLength;
				a_vector[2] *= inverseLength;
			}

			static void WorldToProjectionPoint(const RE::NiTransform& a_world, float* a_point)
			{
				a_point[0] -= a_world.translate.x;
				a_point[1] -= a_world.translate.y;
				a_point[2] -= a_world.translate.z;
			}

			static void WorldToLocalVector(const RE::NiTransform& a_world, float* a_vector)
			{
				const float x =
					(a_world.rotate.entry[0].x * a_vector[0]) +
					(a_world.rotate.entry[1].x * a_vector[1]) +
					(a_world.rotate.entry[2].x * a_vector[2]);
				const float y =
					(a_world.rotate.entry[0].y * a_vector[0]) +
					(a_world.rotate.entry[1].y * a_vector[1]) +
					(a_world.rotate.entry[2].y * a_vector[2]);
				const float z =
					(a_world.rotate.entry[0].z * a_vector[0]) +
					(a_world.rotate.entry[1].z * a_vector[1]) +
					(a_world.rotate.entry[2].z * a_vector[2]);

				a_vector[0] = x;
				a_vector[1] = y;
				a_vector[2] = z;
			}

			[[nodiscard]] static const float* GetBonePalette(RE::BSGeometry* a_geometry)
			{
				auto* skin = a_geometry->skinInstance.get();
				if (!skin)
					return nullptr;

				return *reinterpret_cast<const float**>(reinterpret_cast<std::byte*>(skin) + 0xA0);
			}

			[[nodiscard]] static std::uint32_t GetStride(const VertexDesc a_desc)
			{
				return static_cast<std::uint32_t>(a_desc.desc & 0xF) * 4;
			}

			[[nodiscard]] static std::uint32_t GetAttributeOffsetRaw(std::uint64_t a_desc, Vertex::Attribute a_attribute)
			{
				return static_cast<std::uint32_t>((a_desc >> (4 * static_cast<std::uint8_t>(a_attribute) + 2)) & 0x3C);
			}

			[[nodiscard]] static bool HasVertexPosition(VertexDesc a_desc)
			{
				return a_desc.HasFlag(Vertex::VF_VERTEX);
			}

			[[nodiscard]] static bool HasFullPrecisionFlag(VertexDesc a_desc)
			{
				return a_desc.HasFlag(Vertex::VF_FULLPREC);
			}

			[[nodiscard]] static bool IsStaticFullPrecisionDesc(VertexDesc a_desc)
			{
				return HasVertexPosition(a_desc) && HasFullPrecisionFlag(a_desc);
			}

			[[nodiscard]] static RE::BSGraphics::Renderer* GetRenderer()
			{
				auto* rendererData = RE::BSGraphics::GetRendererData();
				if (!rendererData)
					return nullptr;

				return reinterpret_cast<RE::BSGraphics::Renderer*>(reinterpret_cast<std::byte*>(rendererData) - 0x10);
			}

			[[nodiscard]] static VertexDesc MakeCompactDesc(VertexDesc a_desc)
			{
				const auto oldStride = GetStride(a_desc);
				const auto newStride = oldStride - 8;

				auto compactDesc = a_desc.desc;
				compactDesc &= ~(static_cast<std::uint64_t>(Vertex::VF_FULLPREC) << 44);
				compactDesc = (compactDesc & ~0xFULL) | (newStride / 4);

				for (auto attr = static_cast<std::uint8_t>(Vertex::VA_TEXCOORD0);
					 attr < static_cast<std::uint8_t>(Vertex::VA_COUNT);
					 ++attr)
				{
					const auto attribute = static_cast<Vertex::Attribute>(attr);
					const auto oldOffset = GetAttributeOffsetRaw(a_desc.desc, attribute);
					if (oldOffset >= 8)
						SetAttributeOffsetRaw(compactDesc, attribute, oldOffset - 8);
				}

				return VertexDesc{ compactDesc };
			}

			static void RepackFullPrecisionVertices(
				const std::byte* a_source,
				std::byte* a_destination,
				std::uint32_t a_vertexCount,
				std::uint32_t a_oldStride,
				std::uint32_t a_newStride)
			{
				for (std::uint32_t i = 0; i < a_vertexCount; ++i)
				{
					const auto* source = a_source + (static_cast<std::size_t>(i) * a_oldStride);
					auto* destination = a_destination + (static_cast<std::size_t>(i) * a_newStride);

					std::memset(destination, 0, a_newStride);

					auto* halfPosition = reinterpret_cast<std::uint16_t*>(destination);
					halfPosition[0] = FloatToHalf(*reinterpret_cast<const float*>(source + 0));
					halfPosition[1] = FloatToHalf(*reinterpret_cast<const float*>(source + 4));
					halfPosition[2] = FloatToHalf(*reinterpret_cast<const float*>(source + 8));
					halfPosition[3] = FloatToHalf(*reinterpret_cast<const float*>(source + 12));

					std::memcpy(destination + 8, source + 16, a_oldStride - 16);
				}
			}

			[[nodiscard]] static std::uint16_t GetVertexCount(RE::BSGeometry* a_geometry)
			{
				return *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::byte*>(a_geometry) + 0x164);
			}

			[[nodiscard]] static const std::byte* GetVertexData(TriShapeDataAccess* a_dataAccess)
			{
				if (!a_dataAccess)
					return nullptr;

				const auto* fields = reinterpret_cast<const std::uintptr_t*>(a_dataAccess);
				return reinterpret_cast<const std::byte*>(fields[5]);
			}

			static void WritePackedNormal(const std::byte* a_source, std::uint32_t a_normalOffset, void* a_destination)
			{
				auto* normal = static_cast<float*>(a_destination);
				if (a_normalOffset == 0)
				{
					normal[0] = 0.0F;
					normal[1] = 0.0F;
					normal[2] = 1.0F;
					normal[3] = 0.0F;
					return;
				}

				const auto* packed = reinterpret_cast<const std::uint8_t*>(a_source + a_normalOffset);
				normal[0] = DecodeSnorm8(packed[0]);
				normal[1] = DecodeSnorm8(packed[1]);
				normal[2] = DecodeSnorm8(packed[2]);
				normal[3] = DecodeSnorm8(packed[3]);
			}

			static void ApplyFullPrecisionSkinning(
				RE::BSGeometry* a_geometry,
				void* a_positions,
				void* a_normals,
				const std::byte* a_vertexData,
				std::uint32_t a_vertexCount,
				std::uint32_t a_stride,
				std::uint32_t a_normalOffset,
				std::uint32_t a_skinOffset)
			{
				calculateBoneMatrices(a_geometry);
				const auto* palette = GetBonePalette(a_geometry);
				if (!palette)
					return;

				for (std::uint32_t i = 0; i < a_vertexCount; ++i)
				{
					const auto* vertex = a_vertexData + (static_cast<std::size_t>(i) * a_stride);
					const auto* skin = vertex + a_skinOffset;

					float position[4]{
						*reinterpret_cast<const float*>(vertex + 0),
						*reinterpret_cast<const float*>(vertex + 4),
						*reinterpret_cast<const float*>(vertex + 8),
						1.0F
					};
					float normal[4]{ 0.0F, 0.0F, 1.0F, 0.0F };
					if (a_normalOffset != 0)
					{
						const auto* packed = reinterpret_cast<const std::uint8_t*>(vertex + a_normalOffset);
						normal[0] = DecodeSnorm8(packed[0]);
						normal[1] = DecodeSnorm8(packed[1]);
						normal[2] = DecodeSnorm8(packed[2]);
						normal[3] = 0.0F;
					}

					float weights[4]{
						HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 0)),
						HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 2)),
						HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 4)),
						0.0F
					};
					weights[3] = 1.0F - weights[0] - weights[1] - weights[2];

					const auto* indices = reinterpret_cast<const std::uint8_t*>(skin + 8);
					float skinnedPosition[4]{ 0.0F, 0.0F, 0.0F, 0.0F };
					float skinnedNormal[4]{ 0.0F, 0.0F, 0.0F, 0.0F };

					for (std::uint32_t influence = 0; influence < 4; ++influence)
					{
						const auto weight = weights[influence];
						if (weight <= 0.0F)
							continue;

						const auto* matrix = palette + (static_cast<std::size_t>(indices[influence]) * 12);
						const auto* row0 = matrix + 0;
						const auto* row1 = matrix + 4;
						const auto* row2 = matrix + 8;

						skinnedPosition[0] += weight * Dot4(row0, position);
						skinnedPosition[1] += weight * Dot4(row1, position);
						skinnedPosition[2] += weight * Dot4(row2, position);
						skinnedNormal[0] += weight * Dot3(row0, normal);
						skinnedNormal[1] += weight * Dot3(row1, normal);
						skinnedNormal[2] += weight * Dot3(row2, normal);
					}

					WorldToProjectionPoint(a_geometry->world, skinnedPosition);
					WorldToLocalVector(a_geometry->world, skinnedNormal);

					auto* outPosition = static_cast<float*>(a_positions) + (static_cast<std::size_t>(i) * 4);
					outPosition[0] = skinnedPosition[0];
					outPosition[1] = skinnedPosition[1];
					outPosition[2] = skinnedPosition[2];
					outPosition[3] = 0.0F;

					if (a_normals)
					{
						Normalize3(skinnedNormal);
						auto* outNormal = static_cast<float*>(a_normals) + (static_cast<std::size_t>(i) * 4);
						outNormal[0] = skinnedNormal[0];
						outNormal[1] = skinnedNormal[1];
						outNormal[2] = skinnedNormal[2];
						outNormal[3] = 0.0F;
					}
				}
			}
		}

		using CreateVertexBuffer_t = RE::BSGraphics::VertexBuffer* (*)(
			RE::BSGraphics::Renderer*,
			std::uint32_t*,
			void*,
			std::uint32_t,
			std::uint64_t);
		using CreateTriShape_t = RE::BSGraphics::TriShape* (*)(
			RE::BSGraphics::Renderer*,
			RE::BSGraphics::VertexBuffer*,
			std::uint64_t,
			std::uint16_t*,
			std::uint32_t);
		using BSSubIndexTriShapeCtor_t = RE::BSSubIndexTriShape* (*)(
			void*,
			RE::BSGraphics::TriShape*,
			std::uint64_t,
			std::uint32_t,
			std::uint32_t,
			std::uint32_t);
		using ApplySkinningToGeometry_t = void (*)(
			RE::BSGeometry*,
			void*,
			void*,
			TriShapeDataAccess*);
		using CreatePositionData_t = void* (*)(RE::BSTriShape*);
		using GetTriShapeDataAccess_t = TriShapeDataAccess* (*)(RE::BSTriShape*, bool);
		using CreatePositionDataFromVertexIndexData_t = void* (*)(
			const void*,
			const void*,
			std::uint32_t,
			std::uint32_t,
			const void*,
			bool);
		using DrawSegmentedShape_t = void (*)(RE::BSGeometry*, RE::BSRenderPass*, void*);
		using DecRefRendererData_t = void (*)(void*, void*);

		struct CompactDynamicShapeState
		{
			std::uint64_t originalDesc{ 0 };
			std::uint64_t compactDesc{ 0 };
			std::uint32_t originalVertexBytes{ 0 };
			RE::BSGraphics::VertexBuffer* originalVertexBuffer{ nullptr };
			RE::BSGraphics::VertexBuffer* compactVertexBuffer{ nullptr };
		};

		struct CompactDynamicShapeDrawState
		{
			std::uint64_t compactDesc{ 0 };
			RE::BSGraphics::VertexBuffer* compactVertexBuffer{ nullptr };
		};

		struct DecalPatchState
		{
			bool active{ false };
			std::uint64_t compactDesc{ 0 };
			RE::BSGraphics::VertexBuffer* vertexBuffer{ nullptr };
			RE::BSGraphics::TriShape* triShape{ nullptr };
			std::vector<std::byte> compactVertices;
		};

		struct EffectShaderPatchState
		{
			bool active{ false };
			RE::BSTriShape* shape{ nullptr };
			std::uint64_t originalDesc{ 0 };
			std::uint64_t compactDesc{ 0 };
			std::vector<std::byte> compactData;
		};

		static CreateVertexBuffer_t originalCreateVertexBuffer{ nullptr };
		static CreateTriShape_t originalCreateTriShape{ nullptr };
		static BSSubIndexTriShapeCtor_t originalBSSubIndexTriShapeCtor{ nullptr };
		static ApplySkinningToGeometry_t originalApplySkinningToGeometry{ nullptr };
		static CreatePositionData_t originalCreatePositionData{ nullptr };
		static GetTriShapeDataAccess_t originalEffectShaderGetTriShapeDataAccess{ nullptr };
		static CreatePositionDataFromVertexIndexData_t originalEffectShaderCreatePositionDataFromVertexIndexData{ nullptr };
		static DrawSegmentedShape_t originalDrawSegmentedShape{ nullptr };
		static DecRefRendererData_t originalDecRefRendererData{ nullptr };

		thread_local DecalPatchState decalPatchState;
		thread_local EffectShaderPatchState effectShaderPatchState;
		static std::mutex compactDynamicShapeStatesLock;
		static std::unordered_map<void*, CompactDynamicShapeState> compactDynamicShapeStates;

		static void ApplySkinningToGeometry(
			RE::BSGeometry* a_geometry,
			void* a_positions,
			void* a_normals,
			TriShapeDataAccess* a_dataAccess)
		{
			originalApplySkinningToGeometry(a_geometry, a_positions, a_normals, a_dataAccess);

			if (!a_geometry || !a_positions)
				return;

			const VertexDesc desc{ a_geometry->vertexDesc.desc };
			if (!Utils::HasFullPrecisionFlag(desc))
				return;

			const auto vertexData = Utils::GetVertexData(a_dataAccess);
			const auto vertexCount = Utils::GetVertexCount(a_geometry);
			const auto stride = Utils::GetStride(desc);
			if (!vertexData || vertexCount == 0 || stride < 16)
				return;

			const auto normalOffset = Utils::GetAttributeOffsetRaw(desc.desc, Vertex::VA_NORMAL);
			const auto skinOffset = Utils::GetAttributeOffsetRaw(desc.desc, Vertex::VA_SKINNING);
			if (a_geometry->skinInstance && skinOffset != 0)
			{
				Utils::ApplyFullPrecisionSkinning(
					a_geometry,
					a_positions,
					a_normals,
					vertexData,
					vertexCount,
					stride,
					normalOffset,
					skinOffset);
			}
			else
			{
				for (std::uint32_t i = 0; i < vertexCount; ++i)
				{
					const auto* source = vertexData + (static_cast<std::size_t>(i) * stride);
					auto* position = static_cast<std::byte*>(a_positions) + (static_cast<std::size_t>(i) * 16);
					std::memcpy(position, source, 16);

					if (a_normals)
					{
						auto* normal = static_cast<std::byte*>(a_normals) + (static_cast<std::size_t>(i) * 16);
						Utils::WritePackedNormal(source, normalOffset, normal);
					}
				}
			}
		}

		static RE::BSGraphics::VertexBuffer* CreateVertexBuffer(
			RE::BSGraphics::Renderer* a_renderer,
			std::uint32_t* a_size,
			void* a_data,
			std::uint32_t a_stride,
			std::uint64_t a_desc)
		{
			decalPatchState = {};
			ScopeExit clearStateOnFailure{ []() noexcept { decalPatchState = {}; } };

			VertexDesc oldDesc{ a_desc };
			if (a_data && a_size && a_stride >= 24 && Utils::HasFullPrecisionFlag(oldDesc))
			{
				const auto vertexCount = *a_size / a_stride;
				if (vertexCount > 0)
				{
					const auto compactDesc = Utils::MakeCompactDesc(oldDesc);
					const auto compactStride = Utils::GetStride(compactDesc);
					const auto compactSize = vertexCount * compactStride;

					decalPatchState.compactVertices.resize(compactSize);
					Utils::RepackFullPrecisionVertices(
						static_cast<const std::byte*>(a_data),
						decalPatchState.compactVertices.data(),
						vertexCount,
						a_stride,
						compactStride);

					auto uploadSize = compactSize;
					auto* vertexBuffer = originalCreateVertexBuffer(
						a_renderer,
						std::addressof(uploadSize),
						decalPatchState.compactVertices.data(),
						compactStride,
						compactDesc.desc);

					decalPatchState.active = true;
					decalPatchState.compactDesc = compactDesc.desc;
					decalPatchState.vertexBuffer = vertexBuffer;
					clearStateOnFailure.Release();
					return vertexBuffer;
				}
			}

			return originalCreateVertexBuffer(a_renderer, a_size, a_data, a_stride, a_desc);
		}

		static RE::BSGraphics::TriShape* CreateTriShape(
			RE::BSGraphics::Renderer* a_renderer,
			RE::BSGraphics::VertexBuffer* a_vertexBuffer,
			std::uint64_t a_desc,
			std::uint16_t* a_indices,
			std::uint32_t a_indexCount)
		{
			const bool tracksPatch = decalPatchState.active && decalPatchState.vertexBuffer == a_vertexBuffer;
			ScopeExit clearStateOnFailure{ [tracksPatch]() noexcept {
				if (tracksPatch)
					decalPatchState = {};
			} };

			auto desc = a_desc;
			if (tracksPatch)
				desc = decalPatchState.compactDesc;

			auto* triShape = originalCreateTriShape(a_renderer, a_vertexBuffer, desc, a_indices, a_indexCount);
			if (tracksPatch)
				decalPatchState.triShape = triShape;

			clearStateOnFailure.Release();
			return triShape;
		}

		static RE::BSSubIndexTriShape* BSSubIndexTriShapeCtor(
			void* a_this,
			RE::BSGraphics::TriShape* a_triShape,
			std::uint64_t a_desc,
			std::uint32_t a_vertexCount,
			std::uint32_t a_primitiveCount,
			std::uint32_t a_segmentCount)
		{
			const bool completesPatch = decalPatchState.active && decalPatchState.triShape == a_triShape;
			ScopeExit clearState{ [a_triShape]() noexcept {
				if (decalPatchState.active && decalPatchState.triShape == a_triShape)
					decalPatchState = {};
			} };

			auto desc = a_desc;
			if (completesPatch)
				desc = decalPatchState.compactDesc;

			return originalBSSubIndexTriShapeCtor(
				a_this,
				a_triShape,
				desc,
				a_vertexCount,
				a_primitiveCount,
				a_segmentCount);
		}

		static TriShapeDataAccess* EffectShaderGetTriShapeDataAccess(RE::BSTriShape* a_shape, bool a_arg2)
		{
			if (!effectShaderPatchState.active && a_shape)
			{
				const VertexDesc originalDesc{ a_shape->vertexDesc.desc };
				if (Utils::HasFullPrecisionFlag(originalDesc))
				{
					const auto compactDesc = Utils::MakeCompactDesc(originalDesc);
					effectShaderPatchState = {};
					effectShaderPatchState.active = true;
					effectShaderPatchState.shape = a_shape;
					effectShaderPatchState.originalDesc = originalDesc.desc;
					effectShaderPatchState.compactDesc = compactDesc.desc;
				}
			}

			if (effectShaderPatchState.active && effectShaderPatchState.shape == a_shape)
			{
				const VertexDesc compactDesc{ effectShaderPatchState.compactDesc };
				const VertexDesc originalDesc{ effectShaderPatchState.originalDesc };
				ScopeExit clearStateOnFailure{ [a_shape, originalDesc]() noexcept {
					a_shape->vertexDesc = originalDesc;
					effectShaderPatchState = {};
				} };

				TriShapeDataAccess* result{ nullptr };
				{
					a_shape->vertexDesc = originalDesc;
					ScopeExit restoreCompactDesc{ [a_shape, compactDesc]() noexcept { a_shape->vertexDesc = compactDesc; } };
					result = originalEffectShaderGetTriShapeDataAccess(a_shape, a_arg2);
				}

				if (result)
				{
					const auto* vertexData = Utils::GetVertexData(result);
					const auto vertexCount = a_shape->numVertices;
					const auto indexCount = static_cast<std::uint32_t>(a_shape->numTriangles) * 3;
					const auto originalStride = Utils::GetStride(originalDesc);
					const auto compactStride = Utils::GetStride(compactDesc);

					if (vertexData && vertexCount > 0 && originalStride > compactStride && compactStride >= 8)
					{
						auto* fields = reinterpret_cast<std::uintptr_t*>(result);
						const auto compactVertexBytes = static_cast<std::uint32_t>(vertexCount) * compactStride;
						const auto indexBytes = indexCount * sizeof(std::uint16_t);
						const auto* originalIndexData =
							fields[0] != 0 ?
								vertexData + static_cast<std::uint32_t>(fields[4]) :
								reinterpret_cast<const std::byte*>(fields[6]);

						effectShaderPatchState.compactData.resize(static_cast<std::size_t>(compactVertexBytes) + indexBytes);
						Utils::RepackFullPrecisionVertices(
							vertexData,
							effectShaderPatchState.compactData.data(),
							vertexCount,
							originalStride,
							compactStride);
						if (originalIndexData && indexBytes > 0)
							std::memcpy(effectShaderPatchState.compactData.data() + compactVertexBytes, originalIndexData, indexBytes);

						fields[5] = reinterpret_cast<std::uintptr_t>(effectShaderPatchState.compactData.data());
						if (fields[0] != 0)
						{
							fields[4] = compactVertexBytes;
						}
						else
						{
							fields[6] = reinterpret_cast<std::uintptr_t>(effectShaderPatchState.compactData.data() + compactVertexBytes);
						}

						// Keep the compact descriptor only after repacking so its stride matches the data.
						clearStateOnFailure.Release();
					}
				}

				return result;
			}

			return originalEffectShaderGetTriShapeDataAccess(a_shape, a_arg2);
		}

		static void* EffectShaderCreatePositionDataFromVertexIndexData(
			const void* a_vertices,
			const void* a_indices,
			std::uint32_t a_vertexCount,
			std::uint32_t a_indexCount,
			const void* a_offsets,
			bool a_hasSkinning)
		{
			ScopeExit clearState{ []() noexcept {
				if (effectShaderPatchState.active && effectShaderPatchState.shape)
				{
					effectShaderPatchState.shape->vertexDesc = VertexDesc{ effectShaderPatchState.originalDesc };
					effectShaderPatchState = {};
				}
			} };

			return originalEffectShaderCreatePositionDataFromVertexIndexData(
				a_vertices,
				a_indices,
				a_vertexCount,
				a_indexCount,
				a_offsets,
				a_hasSkinning);
		}

		static void* CreatePositionData(RE::BSTriShape* a_shape)
		{
			if (!a_shape)
				return originalCreatePositionData(a_shape);

			const VertexDesc originalDesc{ a_shape->vertexDesc.desc };
			if (!Utils::HasFullPrecisionFlag(originalDesc))
				return originalCreatePositionData(a_shape);

			const auto compactDesc = Utils::MakeCompactDesc(originalDesc);
			effectShaderPatchState = {};
			effectShaderPatchState.active = true;
			effectShaderPatchState.shape = a_shape;
			effectShaderPatchState.originalDesc = originalDesc.desc;
			effectShaderPatchState.compactDesc = compactDesc.desc;

			a_shape->vertexDesc = compactDesc;
			ScopeExit clearState{ [a_shape, originalDesc]() noexcept {
				a_shape->vertexDesc = originalDesc;
				effectShaderPatchState = {};
			} };

			return originalCreatePositionData(a_shape);
		}

		static bool IsMembraneEffectPass(
			RE::BSRenderPass* a_pass,
			RE::BSShaderProperty*& a_property,
			RE::BSGeometry*& a_geometry,
			VertexDesc& a_desc,
			std::uint32_t& a_technique)
		{
			if (!a_pass)
				return false;

			const auto* pass = reinterpret_cast<const std::byte*>(a_pass);
			a_property = *reinterpret_cast<RE::BSShaderProperty* const*>(pass + 0x10);
			a_geometry = *reinterpret_cast<RE::BSGeometry* const*>(pass + 0x18);
			if (!a_property || !a_property->effectData || !a_geometry || !a_geometry->skinInstance)
				return false;

			a_desc = VertexDesc{ a_geometry->vertexDesc.desc };
			a_technique = *reinterpret_cast<const std::uint32_t*>(pass + 0x48);
			return (a_technique & 0x200u) != 0;
		}

		static bool IsTargetMainEffectPass(
			RE::BSRenderPass* a_pass,
			RE::BSShaderProperty*& a_property,
			RE::BSGeometry*& a_geometry,
			VertexDesc& a_desc,
			std::uint32_t& a_technique)
		{
			if (!IsMembraneEffectPass(a_pass, a_property, a_geometry, a_desc, a_technique) ||
				!Utils::IsStaticFullPrecisionDesc(a_desc))
			{
				return false;
			}

			return true;
		}

		static void ReleaseCompactDynamicShapeState(void* a_rendererData, RE::BSGraphics::Renderer* a_renderer)
		{
			RE::BSGraphics::VertexBuffer* compactVertexBuffer{ nullptr };
			{
				const std::scoped_lock lock{ compactDynamicShapeStatesLock };
				const auto it = compactDynamicShapeStates.find(a_rendererData);
				if (it == compactDynamicShapeStates.end())
					return;

				compactVertexBuffer = it->second.compactVertexBuffer;
				compactDynamicShapeStates.erase(it);
			}

			if (compactVertexBuffer && a_renderer)
				a_renderer->DecRef(compactVertexBuffer);
		}

		static bool GetCompactDynamicShapeState(
			RE::BSGeometry* a_geometry,
			void* a_rendererData,
			CompactDynamicShapeDrawState& a_state)
		{
			if (!a_geometry || !a_rendererData || !originalCreateVertexBuffer)
				return false;

			auto* rendererBytes = static_cast<std::byte*>(a_rendererData);
			const auto originalRendererDesc = *reinterpret_cast<std::uint64_t*>(rendererBytes);
			VertexDesc originalDesc{ originalRendererDesc };
			if (!Utils::IsStaticFullPrecisionDesc(originalDesc))
				return false;

			auto* originalVertexBuffer = *reinterpret_cast<RE::BSGraphics::VertexBuffer**>(rendererBytes + 0x8);
			if (!originalVertexBuffer || !originalVertexBuffer->data)
				return false;

			auto* geometryBytes = reinterpret_cast<std::byte*>(a_geometry);
			const auto vertexCount = *reinterpret_cast<std::uint16_t*>(geometryBytes + 0x164);
			const auto originalStride = Utils::GetStride(originalDesc);
			if (vertexCount == 0 || originalStride < 24)
				return false;

			const auto originalVertexBytes = static_cast<std::uint32_t>(vertexCount) * originalStride;
			if (originalVertexBuffer->dataSize < originalVertexBytes)
				return false;

			auto* renderer = Utils::GetRenderer();
			if (!renderer)
				return false;

			{
				const std::scoped_lock lock{ compactDynamicShapeStatesLock };
				const auto it = compactDynamicShapeStates.find(a_rendererData);
				if (it != compactDynamicShapeStates.end())
				{
					const auto& state = it->second;
					if (state.compactVertexBuffer &&
						state.originalDesc == originalRendererDesc &&
						state.originalVertexBuffer == originalVertexBuffer &&
						state.originalVertexBytes == originalVertexBytes)
					{
						a_state.compactDesc = state.compactDesc;
						a_state.compactVertexBuffer = state.compactVertexBuffer;
						renderer->IncRef(a_state.compactVertexBuffer);
						return true;
					}
				}
			}

			const auto compactDesc = Utils::MakeCompactDesc(originalDesc);
			const auto compactStride = Utils::GetStride(compactDesc);
			const auto compactVertexBytes = static_cast<std::uint32_t>(vertexCount) * compactStride;
			std::vector<std::byte> compactVertices(compactVertexBytes);
			Utils::RepackFullPrecisionVertices(
				static_cast<const std::byte*>(originalVertexBuffer->data),
				compactVertices.data(),
				vertexCount,
				originalStride,
				compactStride);

			auto uploadSize = compactVertexBytes;
			auto* compactVertexBuffer = originalCreateVertexBuffer(
				renderer,
				std::addressof(uploadSize),
				compactVertices.data(),
				compactStride,
				compactDesc.desc);
			if (!compactVertexBuffer)
				return false;

			RE::BSGraphics::VertexBuffer* vertexBufferToRelease{ nullptr };
			{
				const std::scoped_lock lock{ compactDynamicShapeStatesLock };
				auto& state = compactDynamicShapeStates[a_rendererData];
				if (state.compactVertexBuffer &&
					state.originalDesc == originalRendererDesc &&
					state.originalVertexBuffer == originalVertexBuffer &&
					state.originalVertexBytes == originalVertexBytes)
				{
					vertexBufferToRelease = compactVertexBuffer;
					a_state.compactDesc = state.compactDesc;
					a_state.compactVertexBuffer = state.compactVertexBuffer;
				}
				else
				{
					vertexBufferToRelease = state.compactVertexBuffer;
					state = {};
					state.originalDesc = originalRendererDesc;
					state.compactDesc = compactDesc.desc;
					state.originalVertexBytes = originalVertexBytes;
					state.originalVertexBuffer = originalVertexBuffer;
					state.compactVertexBuffer = compactVertexBuffer;
					a_state.compactDesc = state.compactDesc;
					a_state.compactVertexBuffer = state.compactVertexBuffer;
				}

				renderer->IncRef(a_state.compactVertexBuffer);
			}

			if (vertexBufferToRelease)
				renderer->DecRef(vertexBufferToRelease);

			return true;
		}

		static void DrawSegmentedShape(RE::BSGeometry* a_geometry, RE::BSRenderPass* a_pass, void* a_drawData)
		{
			RE::BSShaderProperty* property{ nullptr };
			RE::BSGeometry* passGeometry{ nullptr };
			VertexDesc desc{ 0 };
			std::uint32_t technique{ 0 };
			if (!IsTargetMainEffectPass(a_pass, property, passGeometry, desc, technique) ||
				passGeometry != a_geometry ||
				static_cast<std::uint32_t>(a_geometry->type) != 8)
			{
				originalDrawSegmentedShape(a_geometry, a_pass, a_drawData);
				return;
			}

			auto* geometryBytes = reinterpret_cast<std::byte*>(a_geometry);
			auto* rendererData = *reinterpret_cast<void**>(geometryBytes + 0x148);
			if (!rendererData)
			{
				originalDrawSegmentedShape(a_geometry, a_pass, a_drawData);
				return;
			}

			auto* rendererBytes = static_cast<std::byte*>(rendererData);
			const auto savedDesc = *reinterpret_cast<std::uint64_t*>(rendererBytes);
			CompactDynamicShapeDrawState state;
			if (!GetCompactDynamicShapeState(a_geometry, rendererData, state))
			{
				originalDrawSegmentedShape(a_geometry, a_pass, a_drawData);
				return;
			}

			auto* savedVertexBuffer = *reinterpret_cast<RE::BSGraphics::VertexBuffer**>(rendererBytes + 0x8);
			ScopeExit restoreRendererData{ [rendererBytes, savedDesc, savedVertexBuffer, state]() noexcept {
				*reinterpret_cast<RE::BSGraphics::VertexBuffer**>(rendererBytes + 0x8) = savedVertexBuffer;
				*reinterpret_cast<std::uint64_t*>(rendererBytes) = savedDesc;
				if (auto* renderer = Utils::GetRenderer(); renderer && state.compactVertexBuffer)
					renderer->DecRef(state.compactVertexBuffer);
			} };

			*reinterpret_cast<std::uint64_t*>(rendererBytes) = state.compactDesc;
			*reinterpret_cast<RE::BSGraphics::VertexBuffer**>(rendererBytes + 0x8) = state.compactVertexBuffer;
			originalDrawSegmentedShape(a_geometry, a_pass, a_drawData);
		}

		static void DecRefRendererData(void* a_resourceManager, void* a_rendererData)
		{
			ReleaseCompactDynamicShapeState(a_rendererData, Utils::GetRenderer());
			originalDecRefRendererData(a_resourceManager, a_rendererData);
		}

		struct HookSites
		{
			REL::Relocation<std::uintptr_t> applySkinning;
			REL::Relocation<std::uintptr_t> createVertexBuffer;
			REL::Relocation<std::uintptr_t> createTriShape;
			REL::Relocation<std::uintptr_t> subIndexTriShapeCtor;
			REL::Relocation<std::uintptr_t> effectGetTriShapeDataAccess;
			REL::Relocation<std::uintptr_t> effectCompletion;
			REL::Relocation<std::uintptr_t> drawSegmentedShape;
		};

		[[nodiscard]] static HookSites ResolveHookSites()
		{
			// NG ids were verified to resolve to the same functions on AE, so both share a slot.
			if (RELEX::IsRuntimeOG())
			{
				return {
					REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x136, 0x135 } },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x6D8, 0x67C } },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x6F9, 0x6A6 } },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x78C, 0x742 } },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 1037836, 0 }, 0x37 },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 146786, 2194489 }, 0xA5 },
					REL::Relocation<std::uintptr_t>{ REL::ID{ 1152191, 2318696 }, REL::Offset{ 0x4B7, 0x4BA } }
				};
			}

			return {
				REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x136, 0x135 } },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x6D8, 0x67C } },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x6F9, 0x6A6 } },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 825090, 2212077 }, REL::Offset{ 0x78C, 0x742 } },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 1037836, 2194489 }, 0xD0 },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 0, 2194489 }, 0x155 },
				REL::Relocation<std::uintptr_t>{ REL::ID{ 1152191, 2318696 }, REL::Offset{ 0x4B7, 0x4BA } }
			};
		}

		[[nodiscard]] static bool ValidateCallSite(
			std::string_view a_name,
			const REL::Relocation<std::uintptr_t>& a_site,
			const REL::Relocation<std::uintptr_t>& a_expectedTarget) noexcept
		{
			const auto site = a_site.address();
			const auto opcode = *reinterpret_cast<const std::uint8_t*>(site);
			if (opcode != 0xE8)
			{
				REX::WARN("Full Precision Decals: {} call-site opcode mismatch; expected E8, found {:02X}. Patch was not applied."sv, a_name, opcode);
				return false;
			}

			std::int32_t displacement{ 0 };
			std::memcpy(std::addressof(displacement), reinterpret_cast<const void*>(site + 1), sizeof(displacement));
			const auto actualTarget = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(site + 5) + displacement);
			if (actualTarget != a_expectedTarget.address())
			{
				REX::WARN(
					"Full Precision Decals: {} call target mismatch; expected 0x{:X}, found 0x{:X}. Patch was not applied."sv,
					a_name,
					a_expectedTarget.address(),
					actualTarget);
				return false;
			}

			return true;
		}

		[[nodiscard]] static bool Preflight(
			const HookSites& a_sites,
			bool a_installEffectShaders,
			bool a_installMembrane) noexcept
		{
			if (!ValidateCallSite(
					"decal apply skinning"sv,
					a_sites.applySkinning,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 44611, 2277131 } }) ||
				!ValidateCallSite(
					"decal CreateVertexBuffer"sv,
					a_sites.createVertexBuffer,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 1379034, 2276868 } }) ||
				!ValidateCallSite(
					"decal CreateTriShape"sv,
					a_sites.createTriShape,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 431090, 2276844 } }) ||
				!ValidateCallSite(
					"decal BSSubIndexTriShape constructor"sv,
					a_sites.subIndexTriShapeCtor,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 1068780, 2275989 } }))
			{
				return false;
			}

			if (a_installEffectShaders &&
				!ValidateCallSite(
					"effect-shader TriShapeDataAccess"sv,
					a_sites.effectGetTriShapeDataAccess,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 181124, 2277138 } }))
			{
				return false;
			}

			if (a_installEffectShaders && RELEX::IsRuntimeOG())
			{
				if (!ValidateCallSite(
						"effect-shader CreatePositionData"sv,
						a_sites.effectCompletion,
						REL::Relocation<std::uintptr_t>{ REL::ID{ 1037836, 0 } }))
				{
					return false;
				}
			}
			else if (a_installEffectShaders)
			{
				if (!ValidateCallSite(
						"effect-shader completion helper"sv,
						a_sites.effectCompletion,
						REL::Relocation<std::uintptr_t>{ REL::ID{ 0, 2270283 } }))
				{
					return false;
				}
			}

			if (a_installMembrane &&
				!ValidateCallSite(
					"membrane segmented draw"sv,
					a_sites.drawSegmentedShape,
					REL::Relocation<std::uintptr_t>{ REL::ID{ 673, 2318750 } }))
			{
				return false;
			}

			constexpr std::size_t branchStubSize = 14;
			const std::size_t callCount = 4 + (a_installEffectShaders ? 2 : 0) + (a_installMembrane ? 1 : 0);
			const std::size_t requiredSize = callCount * branchStubSize;
			const auto freeSize = REL::GetTrampoline().free_size();
			if (freeSize < requiredSize)
			{
				REX::WARN(
					"Full Precision Decals: trampoline has {} bytes free; {} are required. Patch was not applied."sv,
					freeSize,
					requiredSize);
				return false;
			}

			return true;
		}

		static void Install(HookSites& a_sites, bool a_installEffectShaders, bool a_installMembrane)
		{
			originalApplySkinningToGeometry = reinterpret_cast<ApplySkinningToGeometry_t>(
				a_sites.applySkinning.write_call<5>(ApplySkinningToGeometry));
			originalCreateVertexBuffer = reinterpret_cast<CreateVertexBuffer_t>(
				a_sites.createVertexBuffer.write_call<5>(CreateVertexBuffer));
			originalCreateTriShape = reinterpret_cast<CreateTriShape_t>(
				a_sites.createTriShape.write_call<5>(CreateTriShape));
			originalBSSubIndexTriShapeCtor = reinterpret_cast<BSSubIndexTriShapeCtor_t>(
				a_sites.subIndexTriShapeCtor.write_call<5>(BSSubIndexTriShapeCtor));
			if (a_installEffectShaders)
			{
				originalEffectShaderGetTriShapeDataAccess = reinterpret_cast<GetTriShapeDataAccess_t>(
					a_sites.effectGetTriShapeDataAccess.write_call<5>(EffectShaderGetTriShapeDataAccess));

				if (RELEX::IsRuntimeOG())
				{
					originalCreatePositionData = reinterpret_cast<CreatePositionData_t>(
						a_sites.effectCompletion.write_call<5>(CreatePositionData));
				}
				else
				{
					originalEffectShaderCreatePositionDataFromVertexIndexData =
						reinterpret_cast<CreatePositionDataFromVertexIndexData_t>(
							a_sites.effectCompletion.write_call<5>(EffectShaderCreatePositionDataFromVertexIndexData));
				}
			}

			if (a_installMembrane)
			{
				originalDrawSegmentedShape = reinterpret_cast<DrawSegmentedShape_t>(
					a_sites.drawSegmentedShape.write_call<5>(DrawSegmentedShape));

				// BSShaderResourceManager vtable: OG 0x30A06A8, NG 0x2706208, AE 0x29139A8; slot 7 is DecRefTriShape.
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSShaderResourceManager[0] };
				originalDecRefRendererData = reinterpret_cast<DecRefRendererData_t>(
					vtable.write_vfunc(7, DecRefRendererData));
			}
		}
	}

	ModuleFullPrecisionDecals::ModuleFullPrecisionDecals() :
		Module("Full Precision Decals", &bFixesFullPrecisionDecals)
	{}

	bool ModuleFullPrecisionDecals::DoQuery() const noexcept
	{
		if (REX::W32::GetModuleHandleW(L"DecalFix.dll"))
		{
			REX::WARN("Full Precision Decals: DecalFix.dll is present; standalone fix already active. Patch was not applied."sv);
			return false;
		}

		return true;
	}

	bool ModuleFullPrecisionDecals::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const bool installEffectShaders = bAdditionalFullPrecisionDecalsEffectShaders.GetValue();
		const bool installMembrane = bAdditionalFullPrecisionDecalsMembrane.GetValue();
		auto sites = fullPrecisionDecalsDetail::ResolveHookSites();
		if (!fullPrecisionDecalsDetail::Preflight(sites, installEffectShaders, installMembrane))
			return false;

		fullPrecisionDecalsDetail::Install(sites, installEffectShaders, installMembrane);
		return true;
	}

	bool ModuleFullPrecisionDecals::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleFullPrecisionDecals::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
