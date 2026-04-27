project(extended_gaussianViewer_app)

file(GLOB SOURCES "*.cpp" "*.h" "*.hpp")
source_group("Source Files" FILES ${SOURCES})

file(GLOB RESOURCES "resources/*.ini")
source_group("Resources Files" FILES ${RESOURCES})

set(WINDOWS_PORTABLE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/windows/run_installed_viewer.cmd")
set(WINDOWS_PORTABLE_CHECK_SCRIPT "${CMAKE_SOURCE_DIR}/tools/windows/check_windows_runtime.ps1")
# set(WINDOWS_PORTABLE_DOC "${CMAKE_SOURCE_DIR}/docs/extended_gaussian_windows_portable_bundle_ko.md")
set(WINDOWS_PORTABLE_EXTRA_RUNTIME_FILES "")
set(WINDOWS_PORTABLE_EXTRA_RUNTIME_IMPORTED_TARGETS "")
set(WINDOWS_PORTABLE_EXTRA_RUNTIME_TARGETS "")

function(append_runtime_target_file_expr target_name files_var)
	if(NOT TARGET ${target_name})
		return()
	endif()

	set(local_files "${${files_var}}")
	list(APPEND local_files "$<TARGET_FILE:${target_name}>")
	list(REMOVE_DUPLICATES local_files)
	set(${files_var} "${local_files}" PARENT_SCOPE)
endfunction()

if(WIN32)
	if(TARGET CUDA::cudart)
		list(APPEND WINDOWS_PORTABLE_EXTRA_RUNTIME_IMPORTED_TARGETS CUDA::cudart)
		append_runtime_target_file_expr(CUDA::cudart WINDOWS_PORTABLE_EXTRA_RUNTIME_FILES)
	endif()

	if(TARGET xatlas)
		list(APPEND WINDOWS_PORTABLE_EXTRA_RUNTIME_TARGETS xatlas)
		append_runtime_target_file_expr(xatlas WINDOWS_PORTABLE_EXTRA_RUNTIME_FILES)
	endif()
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}

	${Boost_LIBRARIES}
	${ASSIMP_LIBRARIES}
	${GLEW_LIBRARIES}
	${OPENGL_LIBRARIES}
	${OpenCV_LIBRARIES}
	OpenMP::OpenMP_CXX
	extended_gaussian
	sibr_view
	sibr_assets
	sibr_renderer
)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "projects/extended_gaussian/apps")

if (WIN32)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
			$<TARGET_FILE_DIR:${PROJECT_NAME}>
		COMMAND_EXPAND_LISTS
	)

	if(WINDOWS_PORTABLE_EXTRA_RUNTIME_FILES)
		add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				${WINDOWS_PORTABLE_EXTRA_RUNTIME_FILES}
				$<TARGET_FILE_DIR:${PROJECT_NAME}>
			COMMAND_EXPAND_LISTS
		)
	endif()

	add_custom_target(${PROJECT_NAME}_extended_gaussian_sync
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:extended_gaussian>
			$<TARGET_FILE_DIR:${PROJECT_NAME}>
		DEPENDS extended_gaussian
	)
	add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_extended_gaussian_sync)
	set_target_properties(${PROJECT_NAME}_extended_gaussian_sync PROPERTIES FOLDER "projects/extended_gaussian/apps")
endif()

if(WIN32)
	if(WINDOWS_PORTABLE_EXTRA_RUNTIME_IMPORTED_TARGETS)
		install(IMPORTED_RUNTIME_ARTIFACTS ${WINDOWS_PORTABLE_EXTRA_RUNTIME_IMPORTED_TARGETS}
			DESTINATION bin
			COMPONENT ${PROJECT_NAME}_install
		)
	endif()

	if(WINDOWS_PORTABLE_EXTRA_RUNTIME_TARGETS)
		install(TARGETS ${WINDOWS_PORTABLE_EXTRA_RUNTIME_TARGETS}
			RUNTIME DESTINATION bin
			COMPONENT ${PROJECT_NAME}_install
		)
	endif()
endif()

## High level macro to install in an homogen way all our ibr targets
include(install_runtime)
ibr_install_target(${PROJECT_NAME}
    INSTALL_PDB                         ## mean install also MSVC IDE *.pdb file (DEST according to target type)
	RESOURCES  	${RESOURCES}
	SCRIPTS     ${WINDOWS_PORTABLE_SCRIPT} ${WINDOWS_PORTABLE_CHECK_SCRIPT}
	RSC_FOLDER 	"extended_gaussian"
    STANDALONE  ${INSTALL_STANDALONE}   ## mean call install_runtime with bundle dependencies resolution
    COMPONENT   ${PROJECT_NAME}_install ## will create custom target to install only this project
)

if (WIN32)
	install(FILES ${WINDOWS_PORTABLE_DOC}
		DESTINATION docs
		COMPONENT ${PROJECT_NAME}_install
	)
endif()
