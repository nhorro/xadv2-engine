# SFML 2.6's font atlas performs GPU-to-GPU texture copies when a page is
# inserted or grows. On Android, its OpenGL ES 1 backend implements those
# copies through framebuffer extension entry points which are not guaranteed
# to exist (and are null on the emulator used by this project).
#
# Constructing Page in place removes the first copy. Preallocating a larger
# atlas makes growth unnecessary for normal game text; if a page is ever full,
# return SFML's existing fallback rectangle rather than entering the unsupported
# copy path. Keep this as a configure-time patch so upgrading SFML either applies
# it reproducibly or fails loudly when the upstream source changes.

if(NOT DEFINED SFML_FONT_CPP)
    message(FATAL_ERROR "SFML_FONT_CPP was not provided")
endif()

file(READ "${SFML_FONT_CPP}" sfml_font_source)
set(sfml_font_changed FALSE)

set(sfml_page_insert_old [=[
    // TODO: Remove this method and use try_emplace instead when updating to C++17
    PageTable::iterator pageIterator = m_pages.find(characterSize);
    if (pageIterator == m_pages.end())
        pageIterator = m_pages.insert(std::make_pair(characterSize, Page(m_isSmooth))).first;
]=])
set(sfml_page_insert_new [=[
    PageTable::iterator pageIterator = m_pages.find(characterSize);
    if (pageIterator == m_pages.end())
        pageIterator = m_pages.try_emplace(characterSize, m_isSmooth).first;
]=])

string(FIND "${sfml_font_source}" "${sfml_page_insert_old}" sfml_page_insert_old_location)
string(FIND "${sfml_font_source}" "${sfml_page_insert_new}" sfml_page_insert_new_location)
if(NOT sfml_page_insert_old_location EQUAL -1)
    string(REPLACE "${sfml_page_insert_old}" "${sfml_page_insert_new}" sfml_font_source "${sfml_font_source}")
    set(sfml_font_changed TRUE)
elseif(sfml_page_insert_new_location EQUAL -1)
    message(FATAL_ERROR "SFML Font.cpp no longer matches the Page insertion patch")
endif()

set(sfml_page_size_old "    image.create(128, 128, Color(255, 255, 255, 0));")
set(sfml_page_size_new "    image.create(1024, 1024, Color(255, 255, 255, 0));")

string(FIND "${sfml_font_source}" "${sfml_page_size_old}" sfml_page_size_old_location)
string(FIND "${sfml_font_source}" "${sfml_page_size_new}" sfml_page_size_new_location)
if(NOT sfml_page_size_old_location EQUAL -1)
    string(REPLACE "${sfml_page_size_old}" "${sfml_page_size_new}" sfml_font_source "${sfml_font_source}")
    set(sfml_font_changed TRUE)
elseif(sfml_page_size_new_location EQUAL -1)
    message(FATAL_ERROR "SFML Font.cpp no longer matches the glyph atlas size patch")
endif()

set(sfml_page_growth_old [=[
            // Not enough space: resize the texture if possible
            unsigned int textureWidth  = page.texture.getSize().x;
            unsigned int textureHeight = page.texture.getSize().y;
            if ((textureWidth * 2 <= Texture::getMaximumSize()) && (textureHeight * 2 <= Texture::getMaximumSize()))
            {
                // Make the texture 2 times bigger
                Texture newTexture;
                newTexture.create(textureWidth * 2, textureHeight * 2);
                newTexture.setSmooth(m_isSmooth);
                newTexture.update(page.texture);
                page.texture.swap(newTexture);
            }
            else
            {
                // Oops, we've reached the maximum texture size...
                err() << "Failed to add a new character to the font: the maximum texture size has been reached" << std::endl;
                return IntRect(0, 0, 2, 2);
            }
]=])
set(sfml_page_growth_new [=[
            // SFML 2.6 cannot preserve an existing font texture while growing
            // it on every OpenGL ES 1 implementation. The Android build starts
            // with a 1024x1024 page, so reaching this fallback is exceptional.
            err() << "Failed to add a new character to the fixed Android font atlas" << std::endl;
            return IntRect(0, 0, 2, 2);
]=])

string(FIND "${sfml_font_source}" "${sfml_page_growth_old}" sfml_page_growth_old_location)
string(FIND "${sfml_font_source}" "${sfml_page_growth_new}" sfml_page_growth_new_location)
if(NOT sfml_page_growth_old_location EQUAL -1)
    string(REPLACE "${sfml_page_growth_old}" "${sfml_page_growth_new}" sfml_font_source "${sfml_font_source}")
    set(sfml_font_changed TRUE)
elseif(sfml_page_growth_new_location EQUAL -1)
    message(FATAL_ERROR "SFML Font.cpp no longer matches the glyph atlas growth patch")
endif()

if(sfml_font_changed)
    file(WRITE "${SFML_FONT_CPP}" "${sfml_font_source}")
    message(STATUS "Android: patched SFML 2.6 font atlas for OpenGL ES")
endif()
