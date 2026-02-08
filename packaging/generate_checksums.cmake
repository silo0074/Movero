# This version looks in the directory where the command is executed
set(TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "Searching for packages in: ${TARGET_DIR}")

file(GLOB PACKAGES "${TARGET_DIR}/*.rpm" "${TARGET_DIR}/*.deb")

if(NOT PACKAGES)
    message(FATAL_ERROR "No .rpm or .deb files found in ${TARGET_DIR}. Run cpack first.")
endif()

foreach(PACKAGE ${PACKAGES})
    get_filename_component(PACKAGE_NAME ${PACKAGE} NAME)
    
    # Avoid checksumming existing .sha256 files
    if(PACKAGE_NAME MATCHES "\\.sha256$")
        continue()
    endif()

    message(STATUS "Calculating SHA256 for: ${PACKAGE_NAME}")
    file(SHA256 "${PACKAGE}" HASH_RESULT)
    
    # Create the individual .sha256 file
    file(WRITE "${PACKAGE}.sha256" "${HASH_RESULT}  ${PACKAGE_NAME}\n")
    
    # Append to a master list
    file(APPEND "${TARGET_DIR}/checksums.txt" "${HASH_RESULT}  ${PACKAGE_NAME}\n")
endforeach()

message(STATUS "Checksums generated successfully.")