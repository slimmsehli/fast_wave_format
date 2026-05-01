

clean:
	rm -rf parser wave *.wave newsim parser_varints parser_masarray *.log

comp_masarray:
	g++ -std=c++20 -O3 -Wall parser_masarray.cpp -o parser_masarray 

run:
	./parser_masarray sim.vcd sim_masarray.wave

comp_varints:
	g++ -std=c++20 -O3 -Wall parser_varints.cpp -o parser_varints 

run_varints:
	./parser_varints sim.vcd sim_varints.wave


comp_read_fwf:
	g++ -std=c++20 -O3 -Wall reader_fwf.cpp -o reader_fwf 

run_read_fwf:
	time ./reader_fwf sim_varints.wave


comp_read_vcd:
	g++ -std=c++20 -O3 -Wall reader_vcd.cpp -o reader_vcd

run_read_vcd:
	time ./reader_vcd sim.vcd

gen:
	g++ -O3 vcd_generator.cpp -o vcd_generator
	./vcd_generator sim_heavy.vcd

bench:
	#g++ -std=c++20 -O3 -Wall parser_masarray.cpp -o parser_masarray 
	#./parser_masarray sim_heavy.vcd sim_heavy.wave 1000000
	#g++ -std=c++20 -O3 -Wall reader_fwf.cpp -o reader_fwf
	time ./reader_fwf sim_heavy.wave
	time ./reader_vcd sim.vcd

# CHUNK_SIZES=(1000 5000 10000 50000 100000 500000 1000000 5000000)
test:
	g++ -std=c++20 -O3 -Wall parser_masarray.cpp -o parser_masarray 
	./parser_masarray sim.vcd sim.wave 10000000 > sim.log &

variant:
	g++ -std=c++20 -O3 -Wall parser_masarray.cpp -o parser_masarray 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_1000.wave 1000 > sim_heavy_masarray_1000.log &
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_5000.wave 5000 > sim_heavy_masarray_5000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_10000.wave 10000 > sim_heavy_masarray_10000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_50000.wave 50000 > sim_heavy_masarray_50000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_100000.wave 100000 > sim_heavy_masarray_100000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_1000000.wave 1000000 > sim_heavy_masarray_1000000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_50000000.wave 50000000 > sim_heavy_masarray_50000000.log & 
	time ./parser_masarray sim_heavy.vcd sim_heavy_masarray_10000000.wave 10000000 > sim_heavy_masarray_10000000.log & 

variant2:
	g++ -std=c++20 -O3 -Wall parser_varints.cpp -o parser_varints 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_1000.wave 1000 > sim_heavy_varints_1000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_5000.wave 5000 > sim_heavy_varints_5000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_10000.wave 10000 > sim_heavy_varints_10000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_50000.wave 50000 > sim_heavy_varints_50000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_100000.wave 100000 > sim_heavy_varints_100000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_1000000.wave 1000000 > sim_heavy_varints_1000000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_10000000.wave 10000000 > sim_heavy_varints_10000000.log & 
	time ./parser_varints sim_heavy.vcd sim_heavy_varints_50000000.wave 50000000 > sim_heavy_varints_50000000.log & 
	
