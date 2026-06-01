add_library(pac_ingreso_urgente_notebook STATIC
    src/notebook/notebook_model.cpp
    src/notebook/notebook_module.cpp
    src/notebook/notebook_scene.cpp
)
target_include_directories(pac_ingreso_urgente_notebook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(TARGET yaml-cpp::yaml-cpp)
    set(PAC_INGRESO_YAMLCPP yaml-cpp::yaml-cpp)
else()
    set(PAC_INGRESO_YAMLCPP yaml-cpp)
endif()
target_link_libraries(pac_ingreso_urgente_notebook
    PUBLIC pac_engine
    PRIVATE ${PAC_INGRESO_YAMLCPP} pac_sol2 pac_project_options)

add_executable(pac_ingreso_urgente main.cpp)
target_link_libraries(pac_ingreso_urgente
    PRIVATE pac_ingreso_urgente_notebook pac_engine pac_project_options)
