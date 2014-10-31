#CXX=clang++
CXX=g++
CXXFLAGS=-g -O0
TEST1_SRCS= ../DummyFlash.cpp ../FlashWearLeveler.cpp test1.cpp
TEST1_OBJS=$(subst .cpp,.o,$(TEST1_SRCS))

all: test1

test1: $(TEST1_OBJS)
	$(CXX) $(LDFLAGS) -o test1 $(TEST1_OBJS) $(LDLIBS) 
	
clean:
	rm -f $(TEST1_OBJS)