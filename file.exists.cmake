if(NOT EXISTS ${FILETOCHECK})
  message(FATAL_ERROR "${FILETOCHECK} does not exist.")
endif()
