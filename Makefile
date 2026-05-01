

clean:
	rm -rf parser
comp:
	g++ -std=c++20 -O3 -Wall parser.cpp -o parser 
	#&& ./vcd_converter