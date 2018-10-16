#
# Doodling
#


# OBJ=list.o interval.o time.o pub.o
OBJ=interval.o pub.o time.o sub.o
TEST_OBJ = interval_test.o pub_test.o list_test.o rmc_test.o sub_test.o rmc_proto.o
HDR=rmc_common.h rmc_list_template.h rmc_pub.h interval.h rmc_proto.h rmc_sub.h


CFLAGS = -g
.PHONY: all clean


all: $(OBJ) rmc_test

rmc_test: $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(OBJ) *~ rmc_test.o rmc_test $(TEST_OBJ)

$(OBJ): $(HDR)

$(TEST_OBJ): $(HDR)
