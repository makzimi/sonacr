find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME recognition_bench_unit_test
         COMMAND Python3::Interpreter -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
set_tests_properties(recognition_bench_unit_test PROPERTIES LABELS tools-unit)

add_test(NAME landmark_density_check
         COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/density_check.py"
                 --db-tool $<TARGET_FILE:local_acr_db>)
set_tests_properties(landmark_density_check PROPERTIES LABELS quality)
