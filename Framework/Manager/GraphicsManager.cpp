#include "GraphicsManager.hpp"

#include <cstring>
#include <iostream>

#include "BRDFIntegrator.hpp"
#include "BaseApplication.hpp"
#include "SceneManager.hpp"

#include "ForwardGeometryPass.hpp"
#include "ShadowMapPass.hpp"
#include "OverlayPass.hpp"

#include "imgui.h"

using namespace My;
using namespace std;

GraphicsManager::GraphicsManager() {
    m_Frames.resize(GfxConfiguration::kMaxInFlightFrameCount);
}

int GraphicsManager::Initialize() {
    int result = 0;

    const GfxConfiguration& conf = m_pApp->GetConfiguration();
    m_pApp->GetFramebufferSize(m_canvasWidth, m_canvasHeight);

    auto pPipelineStateMgr =
        dynamic_cast<BaseApplication*>(m_pApp)->GetPipelineStateManager();

    if (pPipelineStateMgr) {
#if !defined(OS_WEBASSEMBLY)
        m_InitPasses.push_back(
            make_shared<BRDFIntegrator>(this, pPipelineStateMgr));
#endif
        // m_DispatchPasses.push_back(make_shared<RayTracePass>(this,
        // pPipelineStateMgr));
        m_DrawPasses.push_back(
            make_shared<ShadowMapPass>(this, pPipelineStateMgr));

        auto forward_pass = make_shared<ForwardGeometryPass>(this, pPipelineStateMgr);

        forward_pass->EnableRenderToTexture();

        m_DrawPasses.push_back(forward_pass);
        m_DrawPasses.push_back(
            make_shared<OverlayPass>(this, pPipelineStateMgr));
    }

    InitConstants();

    m_bInitialize = true;

    return result;
}

void GraphicsManager::Finalize() {
    EndScene();
}

void GraphicsManager::Tick() {
    auto pSceneManager =
        dynamic_cast<BaseApplication*>(m_pApp)->GetSceneManager();

    if (pSceneManager) {
        auto rev = pSceneManager->GetSceneRevision();
        if (rev == 0) return;  // scene is not loaded yet
        assert(m_nSceneRevision <= rev);
        if (m_nSceneRevision < rev) {
            EndScene();
            cerr << "[GraphicsManager] Detected Scene Change, reinitialize "
                    "buffers "
                    "..."
                 << endl;
            const auto scene = pSceneManager->GetSceneForRendering();
            assert(scene);
            BeginScene(*scene);
            m_nSceneRevision = rev;
        }
    }

    UpdateConstants();

    BeginFrame(m_Frames[m_nFrameIndex]);
    ImGui::NewFrame();
    Draw();
    ImGui::EndFrame();
    ImGui::Render();
    EndFrame(m_Frames[m_nFrameIndex]);

    Present();

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void GraphicsManager::createFramebuffers() {
    assert(m_pApp);
    auto conf = m_pApp->GetConfiguration();

    for (int32_t i = 0; i < GfxConfiguration::kMaxInFlightFrameCount; i++) {
        for (int32_t j = 0; j < m_Frames[i].colorTextures.size(); j++) {
            if (j == 0) {
                ReleaseTexture(m_Frames[i].colorTextures[0]);
            } else {
                if (i == 0) {
                    ReleaseTexture(m_Frames[0].colorTextures[j]);
                }
            }
        }

        m_Frames[i].colorTextures.clear();
    }

    if (m_Frames[0].depthTexture.handler) {
        ReleaseTexture(m_Frames[0].depthTexture);
    }

    for (int32_t i = 0; i < GfxConfiguration::kMaxInFlightFrameCount; i++) {
        Texture2D color_texture;
        color_texture.width = m_canvasWidth;
        color_texture.height = m_canvasHeight;
        color_texture.mips = 1;
        color_texture.pixel_format = PIXEL_FORMAT::RGBA8;
        color_texture.samples = 1;

        GenerateTexture(color_texture);

        m_Frames[i].colorTextures.push_back(color_texture);

        if (i == 0) {
            // Generate msaa intermediate RT
            if (conf.msaaSamples > 1) {
                color_texture.samples = conf.msaaSamples;
                GenerateTexture(color_texture);
                m_Frames[0].colorTextures.push_back(color_texture);
                m_Frames[0].enableMSAA = true;
            }

            // Generate depth RT
            Texture2D depth_buffer;
            depth_buffer.width = m_canvasWidth;
            depth_buffer.height = m_canvasHeight;
            depth_buffer.mips = 1;
            depth_buffer.pixel_format = PIXEL_FORMAT::D32;
            depth_buffer.samples = conf.msaaSamples;

            GenerateTexture(depth_buffer);

            m_Frames[0].depthTexture = depth_buffer;
        } else {
            m_Frames[i].colorTextures.push_back(m_Frames[0].colorTextures[1]);
            m_Frames[i].depthTexture = m_Frames[0].depthTexture;
            m_Frames[i].enableMSAA = m_Frames[0].enableMSAA;
        }
    }
}

void GraphicsManager::ResizeCanvas(int32_t width, int32_t height) {
    if (m_canvasWidth != width || m_canvasHeight != height) {
        cerr << "[GraphicsManager] Resize Canvas to " << width << "x" << height
            << endl;
        m_canvasWidth = width;
        m_canvasHeight = height;

        if (m_bInitialize) {
            // resize frame buffers (for offline-rendering)
            createFramebuffers();
        }
    }
}

void GraphicsManager::UpdateConstants() {
    // update scene object position
    auto& frame = m_Frames[m_nFrameIndex];

    for (auto& pDbc : frame.batchContexts) {
        if (void* rigidBody = pDbc->node->RigidBody()) {
            Matrix4X4f trans;
            BuildIdentityMatrix(trans);

            // the geometry has rigid body bounded, we blend the simlation
            // result here.
            auto pPhysicsManager =
                dynamic_cast<BaseApplication*>(m_pApp)->GetPhysicsManager();

            if (pPhysicsManager) {
                Matrix4X4f simulated_result =
                    dynamic_cast<BaseApplication*>(m_pApp)
                        ->GetPhysicsManager()
                        ->GetRigidBodyTransform(rigidBody);

                // apply the rotation part of the simlation result
                memcpy(trans[0], simulated_result[0], sizeof(float) * 3);
                memcpy(trans[1], simulated_result[1], sizeof(float) * 3);
                memcpy(trans[2], simulated_result[2], sizeof(float) * 3);

                // replace the translation part of the matrix with simlation
                // result directly
                memcpy(trans[3], simulated_result[3], sizeof(float) * 3);
            }

            pDbc->modelMatrix = trans;
        } else {
            pDbc->modelMatrix = *pDbc->node->GetCalculatedTransform();
        }
    }

    // Generate the view matrix based on the camera's position.
    CalculateCameraMatrix();
    CalculateLights();
}

void GraphicsManager::Draw() {
    auto& frame = m_Frames[m_nFrameIndex];

    for (auto& pDispatchPass : m_DispatchPasses) {
        pDispatchPass->BeginPass(frame);
        pDispatchPass->Dispatch(frame);
        pDispatchPass->EndPass(frame);
    }

    for (auto& pDrawPass : m_DrawPasses) {
        pDrawPass->BeginPass(frame);
        pDrawPass->Draw(frame);
        pDrawPass->EndPass(frame);
    }
}

void GraphicsManager::CalculateCameraMatrix() {
    auto pSceneManager =
        dynamic_cast<BaseApplication*>(m_pApp)->GetSceneManager();

    if (pSceneManager) {
        auto& scene = pSceneManager->GetSceneForRendering();
        auto pCameraNode = scene->GetFirstCameraNode();
        DrawFrameContext& frameContext = m_Frames[m_nFrameIndex].frameContext;
        if (pCameraNode) {
            auto transform = *pCameraNode->GetCalculatedTransform();
            Vector3f position =
                Vector3f({transform[3][0], transform[3][1], transform[3][2]});
            Vector3f lookAt = pCameraNode->GetTarget();
            Vector3f up = {0.0f, 0.0f, 1.0f};
            BuildViewRHMatrix(frameContext.viewMatrix, position, lookAt, up);

            frameContext.camPos = {position[0], position[1], position[2], 0.0f};
        } else {
            // use default build-in camera
            Vector3f position = {0.0f, -5.0f, 0.0f},
                     lookAt = {0.0f, 0.0f, 0.0f}, up = {0.0f, 0.0f, 1.0f};
            BuildViewRHMatrix(frameContext.viewMatrix, position, lookAt, up);
        }

        float fieldOfView = PI / 3.0f;
        float nearClipDistance = 10.0f;
        float farClipDistance = 100.0f;

        if (pCameraNode) {
            auto pCamera = scene->GetCamera(pCameraNode->GetSceneObjectRef());
            // Set the field of view and screen aspect ratio.
            fieldOfView =
                dynamic_pointer_cast<SceneObjectPerspectiveCamera>(pCamera)
                    ->GetFov();
            nearClipDistance = pCamera->GetNearClipDistance();
            farClipDistance = pCamera->GetFarClipDistance();
        }


        float screenAspect = (float)m_canvasWidth / (float)m_canvasHeight;

        assert(m_pApp);
        const GfxConfiguration& conf = m_pApp->GetConfiguration();

        // Build the perspective projection matrix.
        if (conf.fixOpenGLPerspectiveMatrix) {
            BuildOpenglPerspectiveFovRHMatrix(
                frameContext.projectionMatrix, fieldOfView, screenAspect,
                nearClipDistance, farClipDistance);
        } else {
            BuildPerspectiveFovRHMatrix(frameContext.projectionMatrix,
                                        fieldOfView, screenAspect,
                                        nearClipDistance, farClipDistance);
        }
    }
}

void GraphicsManager::CalculateLights() {
    DrawFrameContext& frameContext = m_Frames[m_nFrameIndex].frameContext;
    auto& light_info = m_Frames[m_nFrameIndex].lightInfo;

    frameContext.numLights = 0;

    auto pSceneManager =
        dynamic_cast<BaseApplication*>(m_pApp)->GetSceneManager();

    const GfxConfiguration& conf = m_pApp->GetConfiguration();

    if (conf.fixOpenGLPerspectiveMatrix) {
        frameContext.clip_space_type = 0;
    } else {
        frameContext.clip_space_type = 1;
    }

    if (pSceneManager) {
        auto& scene = pSceneManager->GetSceneForRendering();
        for (const auto& LightNode : scene->LightNodes) {
            Light& light = light_info.lights[frameContext.numLights];
            auto pLightNode = LightNode.second.lock();
            if (!pLightNode) continue;
            auto trans_ptr = pLightNode->GetCalculatedTransform();
            light.lightPosition = {0.0f, 0.0f, 0.0f, 1.0f};
            light.lightDirection = {0.0f, 0.0f, -1.0f, 0.0f};
            Transform(light.lightPosition, *trans_ptr);
            Transform(light.lightDirection, *trans_ptr);
            Normalize(light.lightDirection);

            auto pLight = scene->GetLight(pLightNode->GetSceneObjectRef());
            if (pLight) {
                light.lightGuid = pLight->GetGuid();
                light.lightColor = pLight->GetColor().Value;
                light.lightIntensity = pLight->GetIntensity();
                light.lightCastShadow = pLight->GetIfCastShadow();
                const AttenCurve& atten_curve =
                    pLight->GetDistanceAttenuation();
                light.lightDistAttenCurveType = atten_curve.type;
                memcpy(light.lightDistAttenCurveParams, &atten_curve.u,
                       sizeof(atten_curve.u));
                light.lightAngleAttenCurveType = AttenCurveType::kNone;

                Matrix4X4f view;
                Matrix4X4f projection;
                BuildIdentityMatrix(projection);

                float nearClipDistance = 1.0f;
                float farClipDistance = 1000.0f;

                if (pLight->GetType() ==
                    SceneObjectType::kSceneObjectTypeLightInfi) {
                    light.lightType = LightType::Infinity;

                    Vector4f target = {0.0f, 0.0f, 0.0f, 1.0f};

                    auto pCameraNode = scene->GetFirstCameraNode();
                    if (pCameraNode) {
                        auto pCamera =
                            scene->GetCamera(pCameraNode->GetSceneObjectRef());
                        nearClipDistance = pCamera->GetNearClipDistance();
                        farClipDistance = pCamera->GetFarClipDistance();

                        target[2] = -(0.75f * nearClipDistance +
                                      0.25f * farClipDistance);

                        // calculate the camera target position
                        auto trans_ptr = pCameraNode->GetCalculatedTransform();
                        Transform(target, *trans_ptr);
                    }

                    light.lightPosition =
                        target - light.lightDirection * farClipDistance;
                    Vector3f position;
                    position.Set((float*)light.lightPosition);
                    Vector3f lookAt;
                    lookAt.Set((float*)target);
                    Vector3f up = {0.0f, 0.0f, 1.0f};
                    if (abs(light.lightDirection[0]) <= 0.2f &&
                        abs(light.lightDirection[1]) <= 0.2f) {
                        up = {0.1f, 0.1f, 1.0f};
                    }
                    BuildViewRHMatrix(view, position, lookAt, up);

                    float sm_half_dist = min(farClipDistance * 0.25f, 800.0f);

                    if (conf.fixOpenGLPerspectiveMatrix) {
                        BuildOpenglOrthographicRHMatrix(projection, -sm_half_dist,
                                                sm_half_dist, sm_half_dist,
                                                -sm_half_dist, nearClipDistance,
                                                farClipDistance + sm_half_dist);
                    } else {
                        BuildOrthographicRHMatrix(projection, -sm_half_dist,
                                                sm_half_dist, sm_half_dist,
                                                -sm_half_dist, nearClipDistance,
                                                farClipDistance + sm_half_dist);

                    }

                    // notify shader about the infinity light by setting 4th
                    // field to 0
                    light.lightPosition[3] = 0.0f;
                } else {
                    Vector3f position;
                    position.Set(light.lightPosition);
                    Vector4f tmp = light.lightPosition + light.lightDirection;
                    Vector3f lookAt;
                    lookAt.Set(tmp);
                    Vector3f up = {0.0f, 0.0f, 1.0f};
                    if (abs(light.lightDirection[0]) <= 0.1f &&
                        abs(light.lightDirection[1]) <= 0.1f) {
                        up = {0.0f, 0.707f, 0.707f};
                    }
                    BuildViewRHMatrix(view, position, lookAt, up);

                    if (pLight->GetType() ==
                        SceneObjectType::kSceneObjectTypeLightSpot) {
                        light.lightType = LightType::Spot;

                        auto plight =
                            dynamic_pointer_cast<SceneObjectSpotLight>(pLight);
                        const AttenCurve& angle_atten_curve =
                            plight->GetAngleAttenuation();
                        light.lightAngleAttenCurveType = angle_atten_curve.type;
                        memcpy(light.lightAngleAttenCurveParams,
                               &angle_atten_curve.u,
                               sizeof(angle_atten_curve.u));

                        float fieldOfView =
                            light.lightAngleAttenCurveParams[0][1] * 2.0f;
                        float screenAspect = 1.0f;

                        // Build the perspective projection matrix.
                        if (conf.fixOpenGLPerspectiveMatrix) {
                            BuildOpenglPerspectiveFovRHMatrix(
                                projection, fieldOfView, screenAspect,
                                nearClipDistance, farClipDistance);
                        } else {
                            BuildPerspectiveFovRHMatrix(projection,
                                                        fieldOfView, screenAspect,
                                                        nearClipDistance, farClipDistance);
                        }
                    } else if (pLight->GetType() ==
                               SceneObjectType::kSceneObjectTypeLightArea) {
                        light.lightType = LightType::Area;

                        auto plight =
                            dynamic_pointer_cast<SceneObjectAreaLight>(pLight);
                        light.lightSize = plight->GetDimension();
                    } else  // omni light
                    {
                        light.lightType = LightType::Omni;

                        // auto plight =
                        // dynamic_pointer_cast<SceneObjectOmniLight>(pLight);

                        float fieldOfView =
                            PI / 2.0f;  // 90 degree for each cube map face
                        float screenAspect = 1.0f;

                        // Build the perspective projection matrix.
                        if (conf.fixOpenGLPerspectiveMatrix) {
                            BuildOpenglPerspectiveFovRHMatrix(
                                projection, fieldOfView, screenAspect,
                                nearClipDistance, farClipDistance);
                        } else {
                            BuildPerspectiveFovRHMatrix(projection,
                                                        fieldOfView, screenAspect,
                                                        nearClipDistance, farClipDistance);
                        }
                    }
                }

                light.lightViewMatrix = view;
                light.lightProjectionMatrix = projection;
                frameContext.numLights++;
            } else {
                assert(0);
            }
        }
    }
}

void GraphicsManager::BeginScene(const Scene& scene) {
    // first, call init passes on frame 0
    for (const auto& pPass : m_InitPasses) {
        pPass->BeginPass(m_Frames[0]);
        pPass->Dispatch(m_Frames[0]);
        pPass->EndPass(m_Frames[0]);
    }

    // generate shadow map array
    if (!m_Frames[0].frameContext.shadowMap.handler) {
        m_Frames[0].frameContext.shadowMap.width = GfxConfiguration::kShadowMapWidth;
        m_Frames[0].frameContext.shadowMap.height = GfxConfiguration::kShadowMapHeight;
        m_Frames[0].frameContext.shadowMap.size = GfxConfiguration::kMaxShadowMapCount;
        m_Frames[0].frameContext.shadowMap.pixel_format = PIXEL_FORMAT::D32;
        GenerateTextureArray(m_Frames[0].frameContext.shadowMap);
    }

    // generate global shadow map array
    if (!m_Frames[0].frameContext.globalShadowMap.handler) {
        m_Frames[0].frameContext.globalShadowMap.width = GfxConfiguration::kShadowMapWidth;
        m_Frames[0].frameContext.globalShadowMap.height = GfxConfiguration::kShadowMapHeight;
        m_Frames[0].frameContext.globalShadowMap.size = GfxConfiguration::kMaxShadowMapCount;
        m_Frames[0].frameContext.globalShadowMap.pixel_format = PIXEL_FORMAT::D32;
        GenerateTextureArray(m_Frames[0].frameContext.globalShadowMap);
    }

    // generate cube shadow map array
    if (!m_Frames[0].frameContext.cubeShadowMap.handler) {
        m_Frames[0].frameContext.cubeShadowMap.width = GfxConfiguration::kShadowMapWidth;
        m_Frames[0].frameContext.cubeShadowMap.height = GfxConfiguration::kShadowMapHeight;
        m_Frames[0].frameContext.cubeShadowMap.size = GfxConfiguration::kMaxShadowMapCount;
        m_Frames[0].frameContext.cubeShadowMap.pixel_format = PIXEL_FORMAT::D32;
        GenerateCubemapArray(m_Frames[0].frameContext.cubeShadowMap);
    }

    if (scene.Geometries.size()) {
        initializeGeometries(scene);
    }
    if (scene.SkyBox) {
        initializeSkyBox(scene);
    }

    // now, copy the frame structures and initialize shadow maps
    for (int32_t i = 0; i < GfxConfiguration::kMaxInFlightFrameCount; i++) {
        m_Frames[i] = m_Frames[0];
        m_Frames[i].frameIndex = i;
    }

    // generate frame buffers (for offline-rendering)
    // please note we must put this after the copy above to avoid overwritten
    createFramebuffers();
}

void GraphicsManager::EndScene() {
    for (auto& texture : m_Textures) {
        ReleaseTexture(texture);
    }

    for (auto& frame : m_Frames) {
        for (auto& texture : frame.colorTextures) {
            ReleaseTexture(texture);
        }

        ReleaseTexture(frame.depthTexture);
    }
}
