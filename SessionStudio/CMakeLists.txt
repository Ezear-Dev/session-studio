# SessionStudio: the animation editor. Included by SessionAnimLoaderNative/CMakeLists.txt (add_subdirectory).
#   asn_editor: editing model, camera, paths (no interface, tested by asn_tests)
#   SessionStudio: the application (Dear ImGui + ImGuizmo, Direct3D 11) -- added when third_party/ is present
set(ASN_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB ASN_EDITOR_SRC CONFIGURE_DEPENDS ${ASN_EDITOR_DIR}/model/*.cpp ${ASN_EDITOR_DIR}/model/*.h)
add_library(asn_editor STATIC ${ASN_EDITOR_SRC})
# no ${CMAKE_SOURCE_DIR}/src in the path: its math.h would hide the C one (relative includes instead)
target_include_directories(asn_editor PUBLIC ${ASN_EDITOR_DIR}/model)
target_link_libraries(asn_editor PUBLIC asn_core shell32 ole32)

if(EXISTS ${ASN_EDITOR_DIR}/third_party/imgui/imgui.cpp AND EXISTS ${ASN_EDITOR_DIR}/third_party/ImGuizmo/src/ImGuizmo.cpp)
    set(IMGUI ${ASN_EDITOR_DIR}/third_party/imgui)
    file(GLOB ASN_APP_SRC CONFIGURE_DEPENDS ${ASN_EDITOR_DIR}/app/*.cpp ${ASN_EDITOR_DIR}/app/*.h)
    enable_language(RC)   # app/res/SessionStudio.rc: the icon and the file's version details
    add_executable(SessionStudio WIN32 ${ASN_APP_SRC} ${ASN_EDITOR_DIR}/app/res/SessionStudio.rc
        ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
        ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp
        ${ASN_EDITOR_DIR}/third_party/ImGuizmo/src/ImGuizmo.cpp)
    target_include_directories(SessionStudio PRIVATE ${IMGUI} ${IMGUI}/backends ${ASN_EDITOR_DIR}/third_party/ImGuizmo/src ${ASN_EDITOR_DIR}/app)
    target_link_libraries(SessionStudio PRIVATE asn_editor d3d11 dxgi d3dcompiler)
endif()
