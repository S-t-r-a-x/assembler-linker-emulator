# Calls into a library that is partially linked first, so main.o is resolved
# against a section that was already aggregated once.
# Expected: r1=0x00000015, which is (5 + 2) * 3

.extern addTwo, mulThree
.global my_start

.section my_code
my_start:
    ld $stack_top, %sp
    ld $5, %r1
    call addTwo
    call mulThree
    st %r1, result
    halt

.section my_data
result:
.word 0

.section my_stack
.skip 128
stack_top:

.end
