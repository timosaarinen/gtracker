include(FetchContent)

# Sources are pinned by both version URL and SHA-256. An optional local archive
# cache avoids repeat downloads on restricted/offline authoring machines; the
# exact same digest is still checked. Nothing downloads at application runtime.
function(gt_dependency name cache_name url sha)
  set(cache_file "${PROJECT_SOURCE_DIR}/.local/downloads/${cache_name}.tar.gz")
  if(EXISTS "${cache_file}")
    set(source_url "${cache_file}")
  else()
    set(source_url "${url}")
  endif()
  if(ARGV4 STREQUAL "CMAKE")
    FetchContent_Declare(${name} URL "${source_url}" URL_HASH "SHA256=${sha}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  else()
    # These projects are consumed as source files; do not run upstream builds.
    FetchContent_Declare(${name} URL "${source_url}" URL_HASH "SHA256=${sha}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR gtracker-no-cmake-project)
  endif()
  FetchContent_MakeAvailable(${name})
  set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
