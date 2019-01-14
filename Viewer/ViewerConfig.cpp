#include "portable.hpp"
#include "GfxConfiguration.hpp"
#include "Platform/Sdl/OpenGLApplication.hpp"
#if defined(OS_ANDROID) || defined(OS_WEBASSEMBLY)
#include "RHI/OpenGL/OpenGLESConfig.hpp"
#else
#include "RHI/OpenGL/OpenGLConfig.hpp"
#endif
#include "Framework/Common/AssetLoader.hpp"
#include "Framework/Common/MemoryManager.hpp"
#include "Framework/Common/SceneManager.hpp"
#include "Framework/Common/InputManager.hpp"
#include "Framework/Common/AnimationManager.hpp"
#include "Framework/Common/DebugManager.hpp"
#include "Physics/My/MyPhysicsManager.hpp"

#include "ViewerLogic.hpp"

namespace My {
    GfxConfiguration config(8, 8, 8, 8, 24, 8, 0, 960, 540, "Viewer");
	IApplication*    g_pApp             = static_cast<IApplication*>(new OpenGLApplication(config));
    IGameLogic*       g_pGameLogic       = static_cast<IGameLogic*>(new ViewerLogic);
    IPhysicsManager*  g_pPhysicsManager  = static_cast<IPhysicsManager*>(new MyPhysicsManager);
    IMemoryManager*   g_pMemoryManager   = static_cast<IMemoryManager*>(new MemoryManager);
    AssetLoader*     g_pAssetLoader     = static_cast<AssetLoader*>(new AssetLoader);
    SceneManager*    g_pSceneManager    = static_cast<SceneManager*>(new SceneManager);
    InputManager*    g_pInputManager    = static_cast<InputManager*>(new InputManager);
    AnimationManager* g_pAnimationManager = static_cast<AnimationManager*>(new AnimationManager);
#ifdef DEBUG
    DebugManager*    g_pDebugManager    = static_cast<DebugManager*>(new DebugManager);
#endif
}