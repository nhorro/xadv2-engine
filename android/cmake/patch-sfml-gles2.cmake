# Corrections layered over the pinned SFML GLES2 fork. Keep these edits
# configure-time and fail loudly if the pinned dependency stops matching: the
# generated FetchContent checkout must never become an undocumented fork.

if(NOT DEFINED sfml_android_source_dir)
    message(FATAL_ERROR "sfml_android_source_dir was not provided")
endif()

set(sfml_egl "${sfml_android_source_dir}/src/SFML/Window/EglContext.cpp")
file(READ "${sfml_egl}" sfml_egl_source)
set(sfml_egl_old "EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,")
set(sfml_egl_new "EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,")
string(FIND "${sfml_egl_source}" "${sfml_egl_old}" sfml_egl_old_location)
string(FIND "${sfml_egl_source}" "${sfml_egl_new}" sfml_egl_new_location)
if(NOT sfml_egl_old_location EQUAL -1)
    string(REPLACE "${sfml_egl_old}" "${sfml_egl_new}" sfml_egl_source "${sfml_egl_source}")
    file(WRITE "${sfml_egl}" "${sfml_egl_source}")
elseif(sfml_egl_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the GLES2 EGL config patch")
endif()

# The fork destroys the Android window surface before calling setActive(false).
# EglContext::makeCurrent(false) refuses to run once m_surface is EGL_NO_SURFACE,
# so GlContext's thread-local currentContext remains stale. When Android creates
# the replacement surface, setActive(true) then incorrectly becomes a no-op and
# the activity presents black forever. Detach the context while the old surface
# is still valid, then destroy it.
set(sfml_destroy_surface_old [=[
void EglContext::destroySurface()
{
    eglCheck(eglDestroySurface(m_display, m_surface));
    m_surface = EGL_NO_SURFACE;

    // Ensure that this context is no longer active since our surface is now destroyed
    setActive(false);
}
]=])
set(sfml_destroy_surface_new [=[
void EglContext::destroySurface()
{
    // Detach while the old surface is still valid so GlContext clears its
    // thread-local current-context cache before Android replaces the surface.
    setActive(false);
    eglCheck(eglDestroySurface(m_display, m_surface));
    m_surface = EGL_NO_SURFACE;
}
]=])
string(FIND "${sfml_egl_source}" "${sfml_destroy_surface_old}"
       sfml_destroy_surface_old_location)
string(FIND "${sfml_egl_source}" "${sfml_destroy_surface_new}"
       sfml_destroy_surface_new_location)
if(NOT sfml_destroy_surface_old_location EQUAL -1)
    string(REPLACE "${sfml_destroy_surface_old}" "${sfml_destroy_surface_new}"
           sfml_egl_source "${sfml_egl_source}")
    file(WRITE "${sfml_egl}" "${sfml_egl_source}")
elseif(sfml_destroy_surface_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the EGL surface lifecycle patch")
endif()

set(sfml_target_cpp "${sfml_android_source_dir}/src/SFML/Graphics/RenderTarget.cpp")
file(READ "${sfml_target_cpp}" sfml_target_source)
set(sfml_target_ctor_old [=[
m_cache      (),
m_id         (0)
]=])
set(sfml_target_ctor_new [=[
m_cache      (),
m_id         (0),
m_defaultShader(NULL)
]=])
string(FIND "${sfml_target_source}" "${sfml_target_ctor_old}" sfml_ctor_old_location)
string(FIND "${sfml_target_source}" "${sfml_target_ctor_new}" sfml_ctor_new_location)
if(NOT sfml_ctor_old_location EQUAL -1)
    string(REPLACE "${sfml_target_ctor_old}" "${sfml_target_ctor_new}" sfml_target_source "${sfml_target_source}")
    file(WRITE "${sfml_target_cpp}" "${sfml_target_source}")
elseif(sfml_ctor_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the default-shader initialization patch")
endif()

# The fork sends every SFML draw through a shader. Tell our default program
# whether a texture is actually bound so untextured shapes/text geometry render
# with their vertex colour instead of sampling texture object zero. Custom game
# shaders do not declare this engine-only uniform and must not be assigned it.
file(READ "${sfml_target_cpp}" sfml_target_source)
set(sfml_setup_old [=[
        applyShader(shader);

        if(states.texture) {
            shader->setUniform("textMatrix", states.texture->getMatrix(Texture::Pixels));
        }
]=])
set(sfml_setup_new [=[
        applyShader(shader);

        if (!states.shader) {
            shader->setUniform("textureEnabled", states.texture ? 1 : 0);
        }
        if(states.texture) {
            shader->setUniform("textMatrix", states.texture->getMatrix(Texture::Pixels));
        }
]=])
string(FIND "${sfml_target_source}" "${sfml_setup_old}" sfml_setup_old_location)
string(FIND "${sfml_target_source}" "${sfml_setup_new}" sfml_setup_new_location)
if(NOT sfml_setup_old_location EQUAL -1)
    string(REPLACE "${sfml_setup_old}" "${sfml_setup_new}" sfml_target_source "${sfml_target_source}")
    file(WRITE "${sfml_target_cpp}" "${sfml_target_source}")
elseif(sfml_setup_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the texture-enabled shader patch")
endif()

# GLES2 has separate RGB/alpha blend factors and equations in core. The fork
# deliberately disables them on Android as an old GLES1-era workaround. That
# makes an opaque room render texture lose alpha wherever a translucent sprite
# or projected shadow is drawn; compositing it to the window then darkens those
# pixels a second time, producing black sprite fringes and dense shadows.
set(sfml_extensions_hpp
    "${sfml_android_source_dir}/src/SFML/Graphics/GLExtensions.hpp")
file(READ "${sfml_extensions_hpp}" sfml_extensions_source)
set(sfml_blend_old [=[
    // Core since 2.0 - OES_blend_func_separate
    #ifdef SFML_SYSTEM_ANDROID
        // Hack to make transparency working on some Android devices
        #define GLEXT_blend_func_separate                 false
    #else
        #define GLEXT_blend_func_separate                 GL_OES_blend_func_separate
    #endif
]=])
set(sfml_blend_new [=[
    // Core in OpenGL ES 2. Preserve framebuffer alpha independently from RGB.
    #define GLEXT_blend_func_separate                 true
]=])
string(FIND "${sfml_extensions_source}" "${sfml_blend_old}" sfml_blend_old_location)
string(FIND "${sfml_extensions_source}" "${sfml_blend_new}" sfml_blend_new_location)
if(NOT sfml_blend_old_location EQUAL -1)
    string(REPLACE "${sfml_blend_old}" "${sfml_blend_new}"
           sfml_extensions_source "${sfml_extensions_source}")
elseif(sfml_blend_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the separate blend-factor patch")
endif()

set(sfml_equation_old [=[
    // Core since 2.0 - OES_blend_equation_separate
    #ifdef SFML_SYSTEM_ANDROID
        // Hack to make transparency working on some Android devices
        #define GLEXT_blend_equation_separate             false
    #else
        #define GLEXT_blend_equation_separate             GL_OES_blend_equation_separate
    #endif
]=])
set(sfml_equation_new [=[
    // Core in OpenGL ES 2.
    #define GLEXT_blend_equation_separate             true
]=])
string(FIND "${sfml_extensions_source}" "${sfml_equation_old}" sfml_equation_old_location)
string(FIND "${sfml_extensions_source}" "${sfml_equation_new}" sfml_equation_new_location)
if(NOT sfml_equation_old_location EQUAL -1)
    string(REPLACE "${sfml_equation_old}" "${sfml_equation_new}"
           sfml_extensions_source "${sfml_extensions_source}")
elseif(sfml_equation_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the separate blend-equation patch")
endif()
file(WRITE "${sfml_extensions_hpp}" "${sfml_extensions_source}")

# Register the SFML 2.6 MP3 reader added to sfml-audio by the Android CMake
# target. The class header comes from that pinned source-only dependency.
set(sfml_sound_factory
    "${sfml_android_source_dir}/src/SFML/Audio/SoundFileFactory.cpp")
file(READ "${sfml_sound_factory}" sfml_sound_factory_source)
set(sfml_mp3_include_old [=[
#include <SFML/Audio/SoundFileWriterFlac.hpp>
#include <SFML/Audio/SoundFileReaderOgg.hpp>
]=])
set(sfml_mp3_include_new [=[
#include <SFML/Audio/SoundFileWriterFlac.hpp>
#include <SFML/Audio/SoundFileReaderMp3.hpp>
#include <SFML/Audio/SoundFileReaderOgg.hpp>
]=])
string(FIND "${sfml_sound_factory_source}" "${sfml_mp3_include_old}"
       sfml_mp3_include_old_location)
string(FIND "${sfml_sound_factory_source}" "${sfml_mp3_include_new}"
       sfml_mp3_include_new_location)
if(NOT sfml_mp3_include_old_location EQUAL -1)
    string(REPLACE "${sfml_mp3_include_old}" "${sfml_mp3_include_new}"
           sfml_sound_factory_source "${sfml_sound_factory_source}")
elseif(sfml_mp3_include_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the MP3 reader include patch")
endif()

set(sfml_mp3_register_old [=[
            sf::SoundFileFactory::registerWriter<sf::priv::SoundFileWriterFlac>();
            sf::SoundFileFactory::registerReader<sf::priv::SoundFileReaderOgg>();
]=])
set(sfml_mp3_register_new [=[
            sf::SoundFileFactory::registerWriter<sf::priv::SoundFileWriterFlac>();
            sf::SoundFileFactory::registerReader<sf::priv::SoundFileReaderMp3>();
            sf::SoundFileFactory::registerReader<sf::priv::SoundFileReaderOgg>();
]=])
string(FIND "${sfml_sound_factory_source}" "${sfml_mp3_register_old}"
       sfml_mp3_register_old_location)
string(FIND "${sfml_sound_factory_source}" "${sfml_mp3_register_new}"
       sfml_mp3_register_new_location)
if(NOT sfml_mp3_register_old_location EQUAL -1)
    string(REPLACE "${sfml_mp3_register_old}" "${sfml_mp3_register_new}"
           sfml_sound_factory_source "${sfml_sound_factory_source}")
    file(WRITE "${sfml_sound_factory}" "${sfml_sound_factory_source}")
elseif(sfml_mp3_register_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the MP3 reader registration patch")
endif()

# The fork leaves singleInstance pointing at freed WindowImplAndroid storage when
# sf::Window::close() runs. Android destroys the native window immediately after
# main returns and forwards LostFocus through that dangling pointer, causing the
# grey-screen crash/ANR observed after a Back-driven application exit. Backport
# SFML 2.6's destructor reset and null guard so lifecycle callbacks after window
# teardown are harmless. Back itself remains the portable Keyboard::Escape event.
set(sfml_android_window_cpp
    "${sfml_android_source_dir}/src/SFML/Window/Android/WindowImplAndroid.cpp")
file(READ "${sfml_android_window_cpp}" sfml_android_window_source)
set(sfml_window_destructor_old [=[
WindowImplAndroid::~WindowImplAndroid()
{
}
]=])
set(sfml_window_destructor_new [=[
WindowImplAndroid::~WindowImplAndroid()
{
    WindowImplAndroid::singleInstance = NULL;
}
]=])
string(FIND "${sfml_android_window_source}" "${sfml_window_destructor_old}"
       sfml_window_destructor_old_location)
string(FIND "${sfml_android_window_source}" "${sfml_window_destructor_new}"
       sfml_window_destructor_new_location)
if(NOT sfml_window_destructor_old_location EQUAL -1)
    string(REPLACE "${sfml_window_destructor_old}" "${sfml_window_destructor_new}"
           sfml_android_window_source "${sfml_android_window_source}")
elseif(sfml_window_destructor_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the Android window destructor patch")
endif()

set(sfml_forward_event_old [=[
void WindowImplAndroid::forwardEvent(const Event& event)
{
    ActivityStates* states = getActivity(NULL);

    if (event.type == Event::GainedFocus)
    {
        WindowImplAndroid::singleInstance->m_size.x = ANativeWindow_getWidth(states->window);
        WindowImplAndroid::singleInstance->m_size.y = ANativeWindow_getHeight(states->window);
        WindowImplAndroid::singleInstance->m_windowBeingCreated = true;
        WindowImplAndroid::singleInstance->m_hasFocus = true;
    }
    else if (event.type == Event::LostFocus)
    {
        WindowImplAndroid::singleInstance->m_windowBeingDestroyed = true;
        WindowImplAndroid::singleInstance->m_hasFocus = false;
    }

    WindowImplAndroid::singleInstance->pushEvent(event);
}
]=])
set(sfml_forward_event_new [=[
void WindowImplAndroid::forwardEvent(const Event& event)
{
    if (WindowImplAndroid::singleInstance != NULL)
    {
        ActivityStates* states = getActivity(NULL);

        if (event.type == Event::GainedFocus)
        {
            WindowImplAndroid::singleInstance->m_size.x = ANativeWindow_getWidth(states->window);
            WindowImplAndroid::singleInstance->m_size.y = ANativeWindow_getHeight(states->window);
            WindowImplAndroid::singleInstance->m_windowBeingCreated = true;
            WindowImplAndroid::singleInstance->m_hasFocus = true;
        }
        else if (event.type == Event::LostFocus)
        {
            WindowImplAndroid::singleInstance->m_windowBeingDestroyed = true;
            WindowImplAndroid::singleInstance->m_hasFocus = false;
        }

        WindowImplAndroid::singleInstance->pushEvent(event);
    }
}
]=])
string(FIND "${sfml_android_window_source}" "${sfml_forward_event_old}"
       sfml_forward_event_old_location)
string(FIND "${sfml_android_window_source}" "${sfml_forward_event_new}"
       sfml_forward_event_new_location)
if(NOT sfml_forward_event_old_location EQUAL -1)
    string(REPLACE "${sfml_forward_event_old}" "${sfml_forward_event_new}"
           sfml_android_window_source "${sfml_android_window_source}")
    file(WRITE "${sfml_android_window_cpp}" "${sfml_android_window_source}")
elseif(sfml_forward_event_new_location EQUAL -1)
    message(FATAL_ERROR "Pinned SFML no longer matches the Android lifecycle callback patch")
endif()

# Android API 35 marks pollAll unavailable; SFML 2.6 made this same migration.
file(GLOB sfml_android_window_sources
     "${sfml_android_source_dir}/src/SFML/Window/Android/*.cpp")
set(sfml_looper_matches 0)
foreach(sfml_android_window_source IN LISTS sfml_android_window_sources)
    file(READ "${sfml_android_window_source}" sfml_android_window_contents)
    string(FIND "${sfml_android_window_contents}" "ALooper_pollAll(0, NULL, NULL, NULL);"
           sfml_poll_all_location)
    string(FIND "${sfml_android_window_contents}" "ALooper_pollOnce(0, NULL, NULL, NULL);"
           sfml_poll_once_location)
    if(NOT sfml_poll_all_location EQUAL -1)
        string(REPLACE "ALooper_pollAll(0, NULL, NULL, NULL);"
                       "ALooper_pollOnce(0, NULL, NULL, NULL);"
                       sfml_android_window_contents "${sfml_android_window_contents}")
        file(WRITE "${sfml_android_window_source}" "${sfml_android_window_contents}")
        math(EXPR sfml_looper_matches "${sfml_looper_matches} + 1")
    elseif(NOT sfml_poll_once_location EQUAL -1)
        math(EXPR sfml_looper_matches "${sfml_looper_matches} + 1")
    endif()
endforeach()
if(sfml_looper_matches LESS 3)
    message(FATAL_ERROR "Pinned SFML no longer matches the Android looper patch")
endif()

message(STATUS "Android: patched pinned SFML fork for a strict GLES2 context")
