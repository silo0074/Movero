# CPack defines CPACK_PACKAGE_DIRECTORY as the location where the final 
# .rpm or .deb files are placed.
message(STATUS "Post-build checksum script running in: ${CPACK_PACKAGE_DIRECTORY}")

# Look for the packages specifically in the final output directory
file(GLOB PACKAGES 
    "${CPACK_PACKAGE_DIRECTORY}/*.rpm" 
    "${CPACK_PACKAGE_DIRECTORY}/*.deb"
)

if(NOT PACKAGES)
    message(WARNING "No packages found to checksum in ${CPACK_PACKAGE_DIRECTORY}")
endif()

foreach(PACKAGE ${PACKAGES})
    get_filename_component(PACKAGE_NAME ${PACKAGE} NAME)
    
    # Skip checksum files themselves if they exist
    if(PACKAGE_NAME MATCHES "\\.sha256$")
        continue()
    endif()

    message(STATUS "Generating SHA256 for ${PACKAGE_NAME}...")
    file(SHA256 "${PACKAGE}" CHECKSUM)
    
    # Write individual .sha256 file
    file(WRITE "${PACKAGE}.sha256" "${CHECKSUM}  ${PACKAGE_NAME}\n")
    
    # Append to a master checksums.txt in the build folder
    file(APPEND "${CPACK_PACKAGE_DIRECTORY}/checksums.txt" "${CHECKSUM}  ${PACKAGE_NAME}\n")
endforeach()