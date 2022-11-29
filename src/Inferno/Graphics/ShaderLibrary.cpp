#include "pch.h"
#include "ShaderLibrary.h"
#include "Compiler.h"

#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.

//#if defined(_DEBUG)
//// Enable better shader debugging with the graphics debugging tools.
//constexpr UINT COMPILE_FLAGS = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
//#else
//constexpr UINT COMPILE_FLAGS = 0;
//#endif

namespace Inferno {
    using namespace DirectX;
    using namespace DirectX::SimpleMath;

    List<IShader> CompiledShaders;

    // Recompiles a shader
    void CompileShader(IShader* shader) noexcept {
        auto vertexShader = LoadVertexShader(shader->Info.File, shader->RootSignature, shader->Info.VSEntryPoint);
        auto pixelShader = LoadPixelShader(shader->Info.File, shader->Info.PSEntryPoint);
        // Only assign shaders if they compiled successfully
        if (vertexShader && pixelShader) {
            shader->VertexShader = vertexShader;
            shader->PixelShader = pixelShader;
        }
    }

    // Orgb = srgb * Srgb + drgb * Drgb
    const D3D12_RENDER_TARGET_BLEND_DESC BlendDescMultiplyRt = {
        .BlendEnable = true,
        .LogicOpEnable = false,
        .SrcBlend = D3D12_BLEND_DEST_COLOR, // O = S * D
        .DestBlend = D3D12_BLEND_ZERO, // Zero out additive term
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
    };

    const D3D12_BLEND_DESC BlendDescMultiply = {
        .RenderTarget = { BlendDescMultiplyRt }
    };

    constexpr D3D12_DEPTH_STENCIL_DESC DepthEqual =
    {
        TRUE, // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,
        D3D12_COMPARISON_FUNC_EQUAL, // DepthFunc
        FALSE, // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,
        D3D12_DEFAULT_STENCIL_WRITE_MASK,
        {
            D3D12_STENCIL_OP_KEEP, // StencilFailOp
            D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP, // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc
        }, // FrontFace
        {
            D3D12_STENCIL_OP_KEEP, // StencilFailOp
            D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP, // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc
        } // BackFace
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, uint msaaSamples, uint renderTargets) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        if (!shader->RootSignature || !shader->VertexShader || !shader->PixelShader)
            throw Exception("Shader is not valid");

        psoDesc.pRootSignature = shader->RootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(shader->VertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(shader->PixelShader.Get());
        psoDesc.InputLayout = shader->InputLayout;

        psoDesc.RasterizerState = [&effect] {
            switch (effect.Culling) {
                case CullMode::None: return CommonStates::CullNone;
                case CullMode::Clockwise: return CommonStates::CullClockwise;
                case CullMode::CounterClockwise: default: return CommonStates::CullCounterClockwise;
            }
        }();

        psoDesc.BlendState = [&effect] {
            switch (effect.Blend) {
                case BlendMode::Alpha: return CommonStates::AlphaBlend;
                case BlendMode::StraightAlpha: return CommonStates::NonPremultiplied;
                case BlendMode::Additive: return CommonStates::Additive;
                case BlendMode::Opaque: default: return CommonStates::Opaque;
                case BlendMode::Multiply: return BlendDescMultiply;
            }
        }();

        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.DepthStencilState = [&effect] {
            switch (effect.Depth) {
                case DepthMode::None: return CommonStates::DepthNone;
                case DepthMode::ReadWrite: return CommonStates::DepthDefault;
                case DepthMode::Read: default: return CommonStates::DepthRead;
                //case DepthMode::ReadEqual: return DepthEqual;
            };
        }();
        
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = effect.TopologyType;
        psoDesc.NumRenderTargets = renderTargets;

        if (effect.Depth == DepthMode::ReadEqual) {
            // Biases for decals
            psoDesc.RasterizerState.DepthBias = -10000;
            psoDesc.RasterizerState.SlopeScaledDepthBias = -4.0f;
            psoDesc.RasterizerState.DepthBiasClamp = -100000;
        }

        for (uint i = 0; i < renderTargets; i++)
            psoDesc.RTVFormats[i] = shader->Format;

        psoDesc.SampleDesc.Count = effect.EnableMultisample ? msaaSamples : 1;
        return psoDesc;
    }
}

