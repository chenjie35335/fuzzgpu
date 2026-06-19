if(NOT DEFINED DOCS_INDEX_FILE)
  message(FATAL_ERROR "DOCS_INDEX_FILE is required")
endif()

if(NOT DEFINED DOCS_REDIRECT_TARGET)
  set(DOCS_REDIRECT_TARGET "html/index.html")
endif()

get_filename_component(DOCS_INDEX_DIR "${DOCS_INDEX_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${DOCS_INDEX_DIR}")

string(CONFIGURE [=[<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=@DOCS_REDIRECT_TARGET@">
  <title>fuzzgpu documentation</title>
</head>
<body>
  <p><a href="@DOCS_REDIRECT_TARGET@">Open fuzzgpu documentation</a></p>
</body>
</html>
]=] DOCS_REDIRECT_HTML @ONLY)

file(WRITE "${DOCS_INDEX_FILE}" "${DOCS_REDIRECT_HTML}")
