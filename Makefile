
# CHUNK_SIZES=(1000 5000 10000 50000 100000 500000 1000000 5000000)

test_path:=${PWD}/test_dir
misc_path:=${PWD}/misc
converter_path:=${PWD}/converter
reader_path:=${PWD}/reader
binary:=${PWD}/bin
chunks:=10000000

clean:
	rm -rf parser wave *.wave newsim parser_varints parser_masarray *.log test_dir/* bin/*

# generate a test vcd file that has 1GB of data for test
generate_vcd:
	g++ -O3 ${misc_path}/vcd_generator.cpp -o ${binary}/vcd_generator 2>&1 | tee -a ${test_path}/log_vcd_generate.log
	${binary}/vcd_generator ${test_path}/sim.vcd 128 2>&1 | tee -a ${test_path}/log_vcd_generate.log

# compile and run the first alog for compression variant
test_varints:
	g++ -std=c++20 -O3 -Wall ${converter_path}/converter_varints.cpp -o ${binary}/converter_varints 2>&1 | tee -a ${test_path}/log_converter_varints.log
	${binary}/converter_varints ${test_path}/sim.vcd ${test_path}/sim_varints_${chunks}.wave ${chunks} 2>&1 | tee -a ${test_path}/log_converter_varints.log

# compile and run the first alog for compression mas array
test_masarray:
	g++ -std=c++20 -O3 -Wall ${converter_path}/converter_masarray.cpp -o ${binary}/converter_masarray 2>&1 | tee -a ${test_path}/log_converter_masarray.log
	${binary}/converter_masarray ${test_path}/sim.vcd ${test_path}/sim_masarray_${chunks}.wave ${chunks} 2>&1 | tee -a ${test_path}/log_converter_masarray.log

# run the reader from the new wave format  
test_reader_fwf:
	g++ -std=c++20 -O3 -Wall ${reader_path}/reader_fwf.cpp -o ${binary}/reader_fwf 2>&1 | tee -a ${test_path}/log_reader_fwf.log
	${binary}/reader_fwf ${test_path}/sim_varints_${chunks}.wave 2>&1 | tee -a ${test_path}/log_reader_fwf.log

# run the reader from the vcd file format 
test_reader_vcd:
	g++ -std=c++20 -O3 -Wall ${reader_path}/reader_vcd.cpp -o ${binary}/reader_vcd 2>&1 | tee -a ${test_path}/log_reader_vcd.log
	${binary}/reader_vcd ${test_path}/sim.vcd 2>&1 | tee -a ${test_path}/log_reader_vcd.log
