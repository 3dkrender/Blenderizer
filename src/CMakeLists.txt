project(blender)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( blender blender blender.cpp )
target_include_directories( blender PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( blender ${CMAKE_SOURCE_DIR}/../ricardian )