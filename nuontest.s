;r0 = test status register
;r1 = stack pointer (scratch buffer location, 256 bytes minimum)
;r2 = test number
;r3 = saved return address for C calling convention
;r4 - r26: available for test purposes
;r27 = branch target register
;r28 = result register
;r29 = expected result register
;r30 = result flags register
;r31 = expected flags register

;Status of $FEEDF00D indicates that testing is in progress
;Status of $DEADBEEF indicates that a instruction test failed
;Status of $DEADF00D most likely indicates that the implementation
;  of jmp ne, <label>, nop is broken
;Status of $70031337 indicates that all tests passed successfully
;
;The tests attempt to exercise each Nuon instruction form in terms
;of flag setting and operation results.  Flags are always tested
;before the results of the operation are tested.

noflags = $00
zf = $01
cf = $02
vf = $04
nf = $08
mvf = $10
c0zf = $20
c1zf = $40
modgef = $80
modmif = $100
cp0f = $200
cp1f = $400
;Don't include the coprocessor bits or the reserved bits in comparisons
allflags = (zf+cf+vf+nf+mvf+c0zf+c1zf+modgef+modmif)

testStatusReg = r0
scratchBufferReg = r1
testNumberReg = r2
returnAddressReg = r3
branchTargetReg = r27
resultValueReg = r28
expectedResultReg = r29
resultFlagsReg = r30
expectedFlagsReg = r31

.nooptimize

.macro SetTestNumber testNum
	mv_s #testNum, testNumberReg
.mend

.macro SetStatus newStatus
	mv_s #newStatus, testStatusReg
.mend

.macro LoadTestReg value, testReg
	mv_s #value, testReg
.mend

.macro LoadControlReg value, controlReg
  st_s #value, controlReg
  nop
.mend

.macro LoadCounterRegs reg0val, reg1val
	st_s #reg0val, rc0
	st_s #reg1val, rc1
	nop
.mend

.macro LoadFlags value
	st_s #value, cc
	nop
.mend

.macro ReadIndexRegs regX,regY,regU,regV
	ld_s rx, regX
	ld_s ry, regY
	ld_s ru, regU
	ld_s rv, regV
	nop
.mend

.macro LoadIndexReg value, indexReg
	st_s #value, indexReg
	nop
.mend

.macro LoadIndexRegs xval,yval,uval,vval
  st_s #xval, rx
  st_s #yval, ry
  st_s #uval, ru
  st_s #vval, rv
  nop
.mend

.macro ReadCounterRegs regrc0,regrc1
	ld_s rc0, regrc0
	ld_s rc1, regrc1
	nop
.mend

.macro StoreResult testReg
	mv_s testReg, resultValueReg
.mend

;the TestFlags macro automatically adjusts the expectedFlags
;input parameter to account for the current state of the rc0 and
;rc1 counter registers.  If the adjusted expectedFlags do not match
;the value in the resultFlagsReg register, the error handler is
;called

;.macro TestFlags expectedFlags
;	ld_s cc, resultFlagsReg
;	nop
;	mv_s resultFlagsReg, expectedFlagsReg
;	and #(c0zf+c1zf), expectedFlagsReg
;	or #(expectedFlags & ~(c0zf+c1zf)), expectedFlagsReg
;	cmp expectedFlagsReg, resultFlagsReg
;	jmp ne, error, nop
;.mend

.macro TestFlags expectedFlags
	ld_s cc, resultFlagsReg
	nop
  and #allflags, resultFlagsReg
	mv_s #(expectedFlags & allflags), expectedFlagsReg
	cmp expectedFlagsReg, resultFlagsReg
	mv_s #error, branchTargetReg
	jmp ne, (branchTargetReg), nop
.mend

;the TestFlagsExact macro works nearly the same as the TestFlags
;macro except that the expectedFlags parameter is not modified,
;allowing for the c0z and c1z flags to be tested

.macro TestFlagsExact expectedFlags
	ld_s cc, resultFlagsReg
	nop
  and #allflags, resultFlagsReg
	mv_s #(expectedFlags & allflags), expectedFlagsReg
	cmp expectedFlagsReg, resultFlagsReg
	mv_s #error, branchTargetReg
	jmp ne, (branchTargetReg), nop
.mend

.macro TestResult expectedResult
  mv_s #expectedResult, expectedResultReg
	cmp #expectedResult, resultValueReg
	mv_s #error, branchTargetReg
	jmp ne, (branchTargetReg), nop
.mend

.export _nuontest

.text
.align.v

_nuontest:

{
copy r0, scratchBufferReg
ld_s rz, returnAddressReg
}
{
st_v v7, (scratchBufferReg)
add #16, scratchBufferReg
}
{
st_v v6, (scratchBufferReg)
add #16, scratchBufferReg
}
{
st_v v5, (scratchBufferReg)
add #16, scratchBufferReg
}
{
st_v v4, (scratchBufferReg)
add #16, scratchBufferReg
}
{
st_v v3, (scratchBufferReg)
}

SetStatus $DEADBEEF

test_abs:
`test_abs:

;abs(0), expect r4 = 0, zero flag set
SetTestNumber 1
LoadTestReg $0,r4
LoadFlags cf+vf+nf
abs r4
StoreResult r4
TestFlags zf
TestResult $0

;abs($80000000), expect r4 = $80000000, negative, carry and overflow flags set
SetTestNumber 2
LoadTestReg $80000000,r4
LoadFlags zf
abs r4
StoreResult r4
TestFlags cf+vf+nf
TestResult $80000000

;abs($FFFFFFFF), expect r4 = $00000001, carry flag set
SetTestNumber 3
LoadTestReg $FFFFFFFF,r4
LoadFlags zf+vf+nf
abs r4
StoreResult r4
TestFlags cf
TestResult $1
cmp #$1, r1

;abs($7FFFFFFF), expect r4 = $7FFFFFFF, no flags set
SetTestNumber 4
LoadTestReg $7FFFFFFF,r4
LoadFlags zf+vf+nf+cf
abs r4
StoreResult r4
TestFlags noflags
TestResult $7FFFFFFF

`test_addm:
;addm Ri, Rj, Rk
SetTestNumber 5
LoadTestReg 25,r4
LoadTestReg 32,r5
LoadTestReg 0,r6
LoadFlags allflags
addm r4,r5,r6
StoreResult r6
TestFlags allflags
TestResult 57

LoadTestReg 18,r4
LoadTestReg 62,r5
LoadTestReg 255,r6
LoadFlags noflags
addm r4,r5,r6
StoreResult r6
TestFlags noflags
TestResult 80

`test_subm:
;subm #32 - #25, expect #7
SetTestNumber 6
LoadTestReg 25,r4
LoadTestReg 32,r5
LoadTestReg 0,r6
LoadFlags allflags
subm r4,r5,r6
StoreResult r6
TestFlags allflags
TestResult 7

LoadTestReg 18,r4
LoadTestReg 62,r5
LoadTestReg 255,r6
LoadFlags noflags
subm r4,r5,r6
StoreResult r6
TestFlags noflags
TestResult 44

`test_not:
;not($FFFFFFFF): expect r4 = #$0, zf set, cf unchanged, vf cleared
SetTestNumber 7
LoadTestReg $FFFFFFFF,r4
LoadFlags vf+nf
not r4
StoreResult r4
TestFlags zf
TestResult $0

LoadTestReg $FFFFFFFF,r4
LoadFlags vf+nf+cf
not r4
StoreResult r4
TestFlags zf+cf
TestResult $0

;not(0): expect r4 = #$FFFFFFFF, nf set, cf unchanged, vf cleared
SetTestNumber 8
LoadTestReg $0,r4
LoadFlags vf+zf
not r4
StoreResult r4
TestFlags nf
TestResult $FFFFFFFF

LoadTestReg $0,r4
LoadFlags vf+zf+cf
not r4
StoreResult r4
TestFlags nf+cf
TestResult $FFFFFFFF

;not($80000000): expect r4 = #$7FFFFFFF, cf unchanged, vf cleared
SetTestNumber 9
LoadTestReg $80000000,r4
LoadFlags vf+zf+nf
not r4
StoreResult r4
TestFlags noflags
TestResult $7FFFFFFF

LoadTestReg $80000000,r4
LoadFlags vf+zf+nf+cf
not r4
StoreResult r4
TestFlags cf
TestResult $7FFFFFFF

`test_neg:
;neg($FFFFFFFF): expect r4 = #$1, cf set
SetTestNumber 10
LoadTestReg $FFFFFFFF,r4
LoadFlags vf+zf+nf
neg r4
StoreResult r4
TestFlags cf
TestResult $1

;neg($0): expect r4 = #$00000000, zf set
SetTestNumber 11
LoadTestReg $0,r4
LoadFlags vf+cf+zf
neg r4
StoreResult r4
TestFlags zf
TestResult $0

LoadTestReg $0,r4
LoadFlags nf+vf+cf
neg r4
StoreResult r4
TestFlags zf
TestResult $0

;neg($80000000): expect r1 = #$80000000, nf set, vf set, cf set
SetTestNumber 12
LoadTestReg $80000000, r4
LoadFlags zf
neg r4
StoreResult r4
TestFlags nf+vf+cf
TestResult $80000000

;neg($7FFFFFFF): expect r4 = #$80000001, nf set, cf set
SetTestNumber 13
LoadTestReg $7FFFFFFF,r4
LoadFlags zf+vf
neg r4
StoreResult r4
TestFlags nf+cf
TestResult $80000001

`test_copy:

;copy r4=0 to r5, expect r5 = $0, zf set, vf cleared, cf unchanged
SetTestNumber 14
LoadTestReg $0,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags nf+vf
copy r4, r5
StoreResult r5
TestFlags zf
TestResult $0

LoadTestReg $0,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags nf+vf+cf
copy r4, r5
StoreResult r5
TestFlags cf+zf
TestResult $0

;copy r4=$80000000 to r5, expect r5 = $80000000, nf set, vf cleared, cf unchanged
SetTestNumber 15
LoadTestReg $80000000,r4
LoadTestReg $7FFFFFFF,r5
LoadFlags vf+zf
copy r4, r5
StoreResult r5
TestFlags nf
TestResult $80000000

LoadTestReg $80000000,r4
LoadTestReg $7FFFFFFF,r5
LoadFlags vf+cf+zf
copy r4, r5
StoreResult r5
TestFlags nf+cf
TestResult $80000000

;copy r4=$7FFFFFFF to r5, expect r5 = $7FFFFFFF, vf cleared, cf unchanged
SetTestNumber 16
LoadTestReg $7FFFFFFF,r4
LoadTestReg $80000000,r5
LoadFlags vf
copy r4, r5
StoreResult r5
TestFlags noflags
TestResult $7FFFFFFF

LoadTestReg $7FFFFFFF,r4
LoadTestReg $80000000,r5
LoadFlags vf+cf
copy r4, r5
StoreResult r5
TestFlags cf
TestResult $7FFFFFFF

`test_mv_s:

;mv_s Sj, Sk
SetTestNumber 17
LoadTestReg $5A5A5A5A,r4
LoadTestReg $0,r5
LoadFlags noflags
mv_s r4, r5
StoreResult r5
TestFlags noflags
TestResult $5A5A5A5A

;mv_s #n, Sk
SetTestNumber 18
LoadTestReg $F,r4
LoadTestReg $0,r5
LoadFlags allflags
mv_s r4, r5
StoreResult r5
TestFlags allflags
TestResult $F

;mv_s #nnn, Sk ((16 <= nnn <= 2047) or (-2048 <= nnnn <= -17)
SetTestNumber 19
LoadTestReg $FF,r4
LoadTestReg $0,r5
LoadFlags allflags
mv_s r4, r5
StoreResult r5
TestFlags allflags
TestResult $FF

;mv_s #nnnn, Sk ((2048 <= nnn <= (2^31 - 1)) or (-(2^31) <= nnnn <= -2049)
SetTestNumber 20
LoadTestReg $76543210,r4
LoadTestReg $0,r5
LoadFlags allflags
mv_s r4, r5
StoreResult r5
TestFlags allflags
TestResult $76543210

test_mv_v:
`test_mv_v:

;mv_v Vj, Vk
SetTestNumber 21
LoadTestReg $5A5A5A5A,r4
LoadTestReg $A5A5A5A5,r5
LoadTestReg $89ABCDEF,r6
LoadTestReg $01234567,r7
LoadTestReg $0,r8
LoadTestReg $0,r9
LoadTestReg $0,r10
LoadTestReg $0,r11
LoadFlags noflags
mv_v v1, v2
StoreResult r4
TestFlags noflags
TestResult $5A5A5A5A
StoreResult r5
TestResult $A5A5A5A5
StoreResult r6
TestResult $89ABCDEF
StoreResult r7
TestResult $01234567

LoadTestReg $5A5A5A5A,r4
LoadTestReg $A5A5A5A5,r5
LoadTestReg $89ABCDEF,r6
LoadTestReg $01234567,r7
LoadTestReg $0,r8
LoadTestReg $0,r9
LoadTestReg $0,r10
LoadTestReg $0,r11
LoadFlags allflags
mv_v v1, v2
StoreResult r4
TestFlags allflags
TestResult $5A5A5A5A
StoreResult r5
TestResult $A5A5A5A5
StoreResult r6
TestResult $89ABCDEF
StoreResult r7
TestResult $01234567

`test_mvr:

;mvr Sj, RI: expect rx = $12121212, ry = ru = rz = 0
SetTestNumber 22
LoadTestReg $12121212,r4
LoadIndexRegs $0,$0,$0,$0
LoadFlags allflags
mvr r4, rx
LoadTestReg $0,r4
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags allflags
TestResult $12121212
StoreResult r5
TestResult $0
StoreResult r6
TestResult $0
StoreResult r7
TestResult $0

;mvr Sj, RI: expect ry = $12121212, rx = ru = rz = 0
LoadTestReg $12121212,r5
LoadIndexRegs $FFFFFFFF,$0,$FFFFFFFF,$FFFFFFFF
LoadFlags noflags
mvr r5, ry
LoadTestReg $0,r5
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags noflags
TestResult $FFFFFFFF
StoreResult r5
TestResult $12121212
StoreResult r6
TestResult $FFFFFFFF
StoreResult r7
TestResult $FFFFFFFF

;mvr Sj, RI: expect ru = $12121212, rx = ry = rz = 0
LoadTestReg $12121212,r6
LoadIndexRegs $0,$0,$0,$0
LoadFlags noflags
mvr r6, ru
LoadTestReg $0,r6
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags noflags
TestResult $0
StoreResult r5
TestResult $0
StoreResult r6
TestResult $12121212
StoreResult r7
TestResult $0

;mvr Sj, RI: expect rv = $12121212, rx = ry = ru = 0
LoadTestReg $12121212,r7
LoadIndexRegs $0,$0,$0,$0
LoadFlags noflags
mvr r7, rv
LoadTestReg $0,r7
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags noflags
TestResult $0
StoreResult r5
TestResult $0
StoreResult r6
TestResult $0
StoreResult r7
TestResult $12121212

;mvr #nnnn, RI: expect rx = $89ABCDEF, ry = ru = rv = 0
SetTestNumber 23
LoadIndexRegs $0,$0,$0,$0
LoadFlags noflags
mvr #$89ABCDEF, rx
LoadTestReg $0,r4
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags noflags
TestResult $89ABCDEF
StoreResult r5
TestResult $0
StoreResult r6
TestResult $0
StoreResult r7
TestResult $0

`test_dec:

;dec rc0: expect rc0 = $CDEE, rc1 = $5670, cc = $0
SetTestNumber 24
LoadCounterRegs $CDEF,$5670
LoadFlags c0zf+c1zf
dec rc0
ReadCounterRegs r4,r5
StoreResult r4
TestFlagsExact c1zf
TestResult $CDEE
StoreResult r5
TestResult $5670

;dec rc1: expect rc0 = $CDEF, rc1 = $566F, c0zf unchanged, c1zf clear
SetTestNumber 25
LoadCounterRegs $CDEF,$5670
LoadFlags c0zf+c1zf
dec rc1
ReadCounterRegs r4,r5
StoreResult r4
TestFlagsExact c0zf
TestResult $CDEF
StoreResult r5
TestResult $566F

;dec rc0, rc1: expect rc0 = $CDEE, rc1 = $566F, cc = $0
SetTestNumber 26
LoadCounterRegs $CDEF,$5670
LoadFlags c0zf+c1zf
{
dec rc1
dec rc0
}
ReadCounterRegs r4,r5
StoreResult r4
TestFlagsExact noflags
TestResult $CDEE
StoreResult r5
TestResult $566F

;dec rc0, rc1: expect rc0 = $0, rc1 = $0, cc = $60
SetTestNumber 27
LoadCounterRegs $1,$1
LoadFlags noflags
{
dec rc1
dec rc0
}
ReadCounterRegs r4,r5
StoreResult r4
TestFlagsExact c0zf+c1zf
TestResult $0
StoreResult r5
TestResult $0

;dec rc0, rc1: both counters already zero, expect rc0 = $0, rc1 = $0, cc = $60
SetTestNumber 28
LoadCounterRegs $0,$0
LoadFlags noflags
{
dec rc1
dec rc0
}
ReadCounterRegs r4,r5
StoreResult r4
TestFlagsExact c0zf+c1zf
TestResult $0
StoreResult r5
TestResult $0

`test_addr:
SetTestNumber 29

;addr Si, RI
LoadTestReg $12345678,r4
LoadIndexReg $89ABCDEF,rx
LoadFlags allflags
addr r4, rx
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags allflags
TestResult $9BE02467

LoadTestReg $82345678,r4
LoadIndexReg $7DCBA988,ry
LoadFlags noflags
addr r4, ry
ReadIndexRegs r4,r5,r6,r7
StoreResult r5
TestFlags noflags
TestResult $0

;addr #nnnn, RI

SetTestNumber 30

LoadIndexReg $89ABCDEF,ru
LoadFlags allflags
addr #$12345678, ru
ReadIndexRegs r4,r5,r6,r7
StoreResult r6
TestFlags allflags
TestResult $9BE02467

LoadIndexReg $7DCBA988,rv
LoadFlags noflags
addr #$82345678, rv
ReadIndexRegs r4,r5,r6,r7
StoreResult r7
TestFlags noflags
TestResult $0

;addr #(n << 16), RI

SetTestNumber 31

LoadIndexReg $89ABCDEF,rx
LoadFlags allflags
addr #(15 << 16), rx
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags allflags
TestResult $89BACDEF

LoadIndexReg $7DCBA988,ry
LoadFlags noflags
addr #(-16 << 16), ry
ReadIndexRegs r4,r5,r6,r7
StoreResult r5
TestFlags noflags
TestResult $7DBBA988


;modulo RI

`test_modulo:

;less than zero (rx = -10.FFFF, xrange = 521): expect rx = 511.FFFF, modmi set, modge cleared

SetTestNumber 32
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs ((-10 << 16) | $FFFF),(20 << 16),(30 << 16),(40 << 16)
LoadFlags modgef
modulo rx
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags modmif
TestResult ((511 << 16) | $FFFF)

;equal to range (ry = 1022.FFFF, yrange = 1022): expect ry = 0.FFFF, modmi cleared, modge set

SetTestNumber 33
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs (-10 << 16),((1022 << 16) | 0xFFFF),(30 << 16),(40 << 16)
LoadFlags modmif
modulo ry
ReadIndexRegs r4,r5,r6,r7
StoreResult r5
TestFlags modgef
TestResult $0000FFFF

;greater than range (ru = 15.0, urange = 10): expect ru = 5.0, modmi cleared, modge set

SetTestNumber 34
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs 0,0,(15 << 16),0
LoadFlags (allflags & ~modgef)
modulo ru
ReadIndexRegs r4,r5,r6,r7
StoreResult r6
TestFlags (allflags & ~modmif)
TestResult (5 << 16)

;within range (rv = 3.0, vrange = 11): expect rv = 3.0, modmi and modge cleared

SetTestNumber 35
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs (-1 << 16),(-1 << 16),(-1 << 16),(3 << 16)
LoadFlags (modmif|modgef)
modulo rv
ReadIndexRegs r4,r5,r6,r7
StoreResult r7
TestFlags noflags
TestResult (3 << 16)

;range RI

`test_range:

;less than zero (rx = -10.0, xrange = 521): expect rx = -10.0, modmi set, modge cleared

SetTestNumber 36
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs (-10 << 16),(20 << 16),(30 << 16),(40 << 16)
LoadFlags modgef
range rx
ReadIndexRegs r4,r5,r6,r7
StoreResult r4
TestFlags modmif
TestResult (-10 << 16)

;equal to range (ry = 1022.0, yrange = 1022): expect ry = 1022.0, modmi cleared, modge set

SetTestNumber 37
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs (-10 << 16),(1022 << 16),(30 << 16),(40 << 16)
LoadFlags modmif
range ry
ReadIndexRegs r4,r5,r6,r7
StoreResult r5
TestFlags modgef
TestResult (1022 << 16)

;greater than range (ru = 15.0, urange = 10): expect ru = 5.0, modmi cleared, modge set

SetTestNumber 38
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs 0,0,(15 << 16),0
LoadFlags (allflags & ~modgef)
range ru
ReadIndexRegs r4,r5,r6,r7
StoreResult r6
TestFlags (allflags & ~modmif)
TestResult (15 << 16)

;within range (rv = 3.0, vrange = 11): expect rv = 3.0, modmi and modge cleared

SetTestNumber 39
;xrange = 521, yrange = 1022
LoadControlReg ((521 << 16) | (1022 << 0)), xyrange
;urange = 10, vrange = 11
LoadControlReg ((10 << 16) | (11 << 0)), uvrange

LoadIndexRegs (-1 << 16),(-1 << 16),(-1 << 16),(3 << 16)
LoadFlags (modmif|modgef)
range rv
ReadIndexRegs r4,r5,r6,r7
StoreResult r7
TestFlags noflags
TestResult (3 << 16)

`test_msb:

;msb Si, Sk
SetTestNumber 40
LoadTestReg $0,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags noflags
msb r4, r5
StoreResult r5
TestFlags zf
TestResult $0

LoadTestReg $FFFFFFFF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~zf)
msb r4, r5
StoreResult r5
TestFlags allflags
TestResult $0

LoadTestReg $0007F00F,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags noflags
msb r4, r5
StoreResult r5
TestFlags noflags
TestResult 19

LoadTestReg $FFFFF90C,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags noflags
msb r4, r5
StoreResult r5
TestFlags noflags
TestResult 11

`test_sat:

;sat #n, Si, Sk

;positive saturation; expect r5 = $7FFF, nf and zf cleared, cf and vf unchanged
SetTestNumber 41
LoadTestReg $04F08310,r4
LoadTestReg $FFFF8000,r5
LoadFlags zf+nf+cf+vf
sat #16, r4, r5
StoreResult r5
TestFlags cf+vf
TestResult $7FFF

;negative saturation; expect r5 = $FFFF8000, nf set, zf cleared
LoadTestReg $FF000000,r4
LoadTestReg $00007FFF,r5
LoadFlags zf
sat #16, r4, r5
StoreResult r5
TestFlags nf
TestResult $FFFF8000

;no saturation: expect r5 = $FFFFFFFD, nf set, zf cleared
LoadTestReg $FFFFFFFD,r4
LoadTestReg $00000002,r5
LoadFlags zf
sat #3, r4, r5
StoreResult r5
TestFlags nf
TestResult $FFFFFFFD

;no saturation: expect r5 = $0, nf cleared, zf set
LoadTestReg $0,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags nf
sat #3, r4, r5
StoreResult r5
TestFlags zf
TestResult $0

`test_bits:

;bits #n, >>#m, Sk

;bits #31, >>#0, $89ABCDEF, expect r4 =$89ABCDEF , nf set, zf cleared
SetTestNumber 42
LoadTestReg $89ABCDEF,r4
LoadFlags (allflags & ~nf)
bits #31, >>#0, r4
StoreResult r4
TestFlags (allflags & ~zf)
TestResult $89ABCDEF

;bits #31, >>#31, $7FFFFFFF, expect r4 = 0 , zf set
LoadTestReg $7FFFFFFF,r4
LoadFlags noflags
bits #31, >>#31, r4
StoreResult r4
TestFlags zf
TestResult 0

;bits #5, >>#8, $FFFFF5FF, expect r4 = $00000035, nf cleared, zf cleared
LoadTestReg $FFFFF5FF,r4
LoadFlags nf+zf
bits #5, >>#8, r4
StoreResult r4
TestFlags noflags
TestResult $35

;bits #n, >>Si, Sk

;bits #31, >>#0, $89ABCDEF, expect r4 =$89ABCDEF , nf set, zf cleared
SetTestNumber 43
LoadTestReg $89ABCDEF,r4
LoadTestReg 0, r5
LoadFlags (allflags & ~nf)
bits #31, >>r5, r4
StoreResult r4
TestFlags (allflags & ~zf)
TestResult $89ABCDEF

;bits #31, >>#31, $7FFFFFFF, expect r4 = 0 , zf set
LoadTestReg $7FFFFFFF,r4
LoadTestReg 31,r5
LoadFlags noflags
bits #31, >>r5, r4
StoreResult r4
TestFlags zf
TestResult 0

;bits #5, >>#8, $FFFFF5FF, expect r4 = $00000035, nf cleared, zf cleared
LoadTestReg $FFFFF5FF,r4
LoadTestReg 8, r5
LoadFlags nf+zf
bits #5, >>r5, r4
StoreResult r4
TestFlags noflags
TestResult $35

`test_add_p:

;add_p Vi,Vj,Vk

;add_p v1,v2,v3
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;v3 = ($FFFFFFFF, $0000FFFF, $FFFFFFFF, $A5A5A5A5)
;expect v3 = ($89BD0000, $FFFF0000, $34560000, $A5A5A5A5)
SetTestNumber 44
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadTestReg $FFFFFFFF,r12
LoadTestReg $0000FFFF,r13
LoadTestReg $FFFFFFFF,r14
LoadTestReg $A5A5A5A5,r15
LoadFlags (allflags)
add_p v1,v2,v3
StoreResult r12
TestFlags (allflags)
TestResult $89BD0000
StoreResult r13
TestResult $FFFF0000
StoreResult r14
TestResult $34560000
StoreResult r15
TestResult $A5A5A5A5

`test_add_sv:

;add_sv Vi,Vk

;add_sv v1,v2
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;expect v2 = ($89BD0000, $FFFF0000, $34560000, $FFFF0000)
SetTestNumber 45
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadFlags (allflags)
add_sv v1,v2
StoreResult r8
TestFlags (allflags)
TestResult $89BD0000
StoreResult r9
TestResult $FFFF0000
StoreResult r10
TestResult $34560000
StoreResult r11
TestResult $FFFF0000

;add_sv Vi,Vj,Vk

;add_sv v1,v2,v3
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;v3 = ($FFFFFFFF, $0000FFFF, $FFFFFFFF, $0000FFFF)
;expect v3 = ($89BD0000, $FFFF0000, $34560000, $FFFF0000)
SetTestNumber 46
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadTestReg $FFFFFFFF,r12
LoadTestReg $0000FFFF,r13
LoadTestReg $FFFFFFFF,r14
LoadTestReg $0000FFFF,r15
LoadFlags (allflags)
add_sv v1,v2,v3
StoreResult r12
TestFlags (allflags) 
TestResult $89BD0000
StoreResult r13
TestResult $FFFF0000
StoreResult r14
TestResult $34560000
StoreResult r15
TestResult $FFFF0000

test_sub_p:
`test_sub_p:

;sub_p Vi,Vj,Vk

;sub_p v1,v2,v3
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;v3 = ($FFFFFFFF, $0000FFFF, $FFFFFFFF, $A5A5A5A5)
;expect v3 = ($65550000, $ECA90000, $CBAA0000, $A5A5A5A5) 
SetTestNumber 47
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadTestReg $FFFFFFFF,r12
LoadTestReg $0000FFFF,r13
LoadTestReg $FFFFFFFF,r14
LoadTestReg $A5A5A5A5,r15
LoadFlags (allflags)
sub_p v1,v2,v3
StoreResult r12
TestFlags (allflags) 
TestResult $65550000
StoreResult r13
TestResult $ECA90000
StoreResult r14
TestResult $CBAA0000
StoreResult r15
TestResult $A5A5A5A5

test_sub_sv:
`test_sub_sv:

;sub_sv Vi,Vk

;sub_sv v1,v2
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;expect v2 = ($65550000, $ECA90000, $CBAA0000, $579BA5A5)
SetTestNumber 48
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadFlags (allflags)
sub_sv v1,v2
StoreResult r8
TestFlags (allflags)
TestResult $65550000
StoreResult r9
TestResult $ECA90000
StoreResult r10
TestResult $CBAA0000
StoreResult r11
TestResult $579B0000

;sub_sv Vi,Vj,Vk

;sub_sv v1,v2,v3
;v1 = ($1234FFFE, $89ABCDEF, $34561234, $54321000)
;v2 = ($77890001, $76543211, $0000FFFF, $ABCDEFFF)
;v3 = ($FFFFFFFF, $0000FFFF, $FFFFFFFF, $A5A5A5A5)
;expect v3 = ($65550000, $ECA90000, $CBAA0000, $579BA5A5)
SetTestNumber 49
LoadTestReg $1234FFFE,r4
LoadTestReg $89ABCDEF,r5
LoadTestReg $34561234,r6
LoadTestReg $54321000,r7
LoadTestReg $77890001,r8
LoadTestReg $76543211,r9
LoadTestReg $0000FFFF,r10
LoadTestReg $ABCDEFFF,r11
LoadTestReg $FFFFFFFF,r12
LoadTestReg $0000FFFF,r13
LoadTestReg $FFFFFFFF,r14
LoadTestReg $A5A5A5A5,r15
LoadFlags (allflags)
sub_sv v1,v2,v3
StoreResult r12
TestFlags (allflags)
TestResult $65550000
StoreResult r13
TestResult $ECA90000
StoreResult r14
TestResult $CBAA0000
StoreResult r15
TestResult $579B0000

`test_ls:

;ls >>Sj, Si, Sk

;ls >>5, $84A5A51E, r6: expect r6 = $4252D28, nf, zf, cf and vf cleared
SetTestNumber 50
LoadTestReg 5,r4
LoadTestReg $84A5A51E,r5
LoadTestReg $0,r6
LoadFlags (allflags)
ls >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(nf+vf+cf+zf))
TestResult $04252D28

;ls >>31, $700FF00F, r6: expect r6 = $0, nf, vf cleared, zf, cf set
LoadTestReg 31,r4
LoadTestReg $700FF00F,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (vf+nf)
ls >>r4, r5, r6
StoreResult r6
TestFlags cf+zf
TestResult 0

;ls >>-32, $7FFFFFFF, r6: expect r6 = $0, nf, cf, vf cleared, zf set
LoadTestReg -32,r4
LoadTestReg $7FFFFFFF,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (vf+cf+nf)
ls >>r4, r5, r6
StoreResult r6
TestFlags zf
TestResult $0

;ls >>-31, $80000001, r6: expect r6 = $80000000, vf, zf cleared, cf, nf set
LoadTestReg -31,r4
LoadTestReg $80000001,r5
LoadTestReg $7FFFFFFE,r6
LoadFlags (allflags & ~nf)
ls >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $80000000

;ls >>-2, $80000001, r6: expect r6 = $4, vf, zf, nf cleared, cf set
LoadTestReg -2,r4
LoadTestReg $80000001,r5
LoadTestReg $7FFFFFFE,r6
LoadFlags (allflags & ~cf)
ls >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf+nf))
TestResult $4

`test_as:

;as >>Sj, Si, Sk

;as >>5, $84A5A51E, r6: expect r6 = $FC252D28, zf, cf and vf cleared, nf set
SetTestNumber 51
LoadTestReg 5,r4
LoadTestReg $84A5A51E,r5
LoadTestReg $0,r6
LoadFlags (allflags & ~nf)
as >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(vf+cf+zf))
TestResult $FC252D28

;as >>31, $0FFFFF01, r6: expect r6 = $0, nf, vf cleared, zf, cf set
LoadTestReg 31,r4
LoadTestReg $0FFFFF01,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (nf+vf)
as >>r4, r5, r6
StoreResult r6
TestFlags cf+zf
TestResult 0

;as >>-32, $7FFFFFFF, r6: expect r6 = $0, nf, cf, vf cleared, zf set
LoadTestReg -32,r4
LoadTestReg $7FFFFFFF,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (nf+cf+vf)
as >>r4, r5, r6
StoreResult r6
TestFlags zf
TestResult $0

;as >>-31, $80000001, r6: expect r6 = $80000000, vf, zf cleared, cf, nf set
LoadTestReg -31,r4
LoadTestReg $80000001,r5
LoadTestReg $7FFFFFFE,r6
LoadFlags (allflags & ~(cf+nf))
as >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $80000000

;as >>-2, $80000001, r6: expect r6 = $4, vf, zf, nf cleared, cf set
LoadTestReg -2,r4
LoadTestReg $80000001,r5
LoadTestReg $7FFFFFFE,r6
LoadFlags (allflags & ~cf)
as >>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf+nf))
TestResult $4

`test_lsr:

;lsr #m, Si, Sk

;lsr #5, $84A5A51E, r5: expect r5 = $4252D28, nf, zf, cf and vf cleared
SetTestNumber 52
LoadTestReg $84A5A51E,r4
LoadTestReg $0,r5
LoadFlags (allflags)
lsr #5, r4, r5
StoreResult r5
TestFlags (allflags & ~(nf+vf+cf+zf))
TestResult $04252D28

;lsr #31, $700FF00F, r5: expect r6 = $0, nf, vf cleared, zf, cf set
LoadTestReg $700FF00F,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (nf+vf)
lsr #31, r4, r5
StoreResult r5
TestFlags cf+zf
TestResult 0

;lsr #30, $3FFFFFFE, r5: expect r5 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFE,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (vf+cf+nf)
lsr #30, r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;lsr #0, $80000000, r5: expect r5 = $80000000, vf, zf, cf cleared, nf set
LoadTestReg $80000000,r4
LoadTestReg $7FFFFFFF,r5
LoadFlags (allflags & ~nf)
lsr #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf+cf))
TestResult $80000000

;lsr #0, $80000001, r5: expect r5 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadTestReg $7FFFFFFE,r5
LoadFlags (allflags & ~(nf+cf))
lsr #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

;lsr #m, Sk

;lsr #5, $84A5A51E: expect r4 = $4252D28, nf, zf, cf and vf cleared
SetTestNumber 53
LoadTestReg $84A5A51E,r4
LoadFlags (allflags)
lsr #5, r4
StoreResult r4
TestFlags (allflags & ~(nf+vf+cf+zf))
TestResult $04252D28

;lsr #31, $700FF00F: expect r4 = $0, nf, vf cleared, zf, cf set
LoadTestReg $700FF00F,r4
LoadFlags (zf+cf)
lsr #31, r4
StoreResult r4
TestFlags cf+zf
TestResult 0

;lsr #30, $3FFFFFFE: expect r4 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFE,r4
LoadFlags (vf+cf+nf)
lsr #30, r4
StoreResult r4
TestFlags zf
TestResult 0

;lsr #0, $80000000, r4: expect r4 = $80000001, vf, zf, cf cleared, nf set
LoadTestReg $80000000,r4
LoadFlags (allflags & ~nf)
lsr #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf+cf))
TestResult $80000000

;lsr #0, $80000001: expect r4 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadFlags (allflags & ~(nf+cf))
lsr #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

`test_asr:

;asr #m, Si, Sk

;asr #5, $84A5A51E, r5: expect r5 = $FC252D28, zf, cf and vf cleared, nf set
SetTestNumber 54
LoadTestReg $84A5A51E,r4
LoadTestReg $0,r5
LoadFlags (allflags & ~nf)
asr #5, r4, r5
StoreResult r5
TestFlags (allflags & ~(vf+cf+zf))
TestResult $FC252D28

;asr #31, $700FF00F, r5: expect r6 = $0, nf, vf cleared, zf, cf set
LoadTestReg $700FF00F,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (nf+vf)
asr #31, r4, r5
StoreResult r5
TestFlags cf+zf
TestResult 0

;asr #30, $3FFFFFFE, r5: expect r5 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFE,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (nf+cf+vf)
asr #30, r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;asr #0, $80000000, r5: expect r5 = $80000001, vf, zf, cf cleared, nf set
LoadTestReg $80000000,r4
LoadTestReg $7FFFFFFE,r5
LoadFlags (allflags)
asr #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf+cf))
TestResult $80000000

;asr #0, $80000001, r5: expect r5 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadTestReg $7FFFFFFE,r5
LoadFlags (allflags)
asr #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

;asr #m, Sk

;asr #5, $84A5A51E: expect r4 = $FC252D28, zf, cf and vf cleared, nf set
SetTestNumber 55
LoadTestReg $84A5A51E,r4
LoadFlags (allflags & ~nf)
asr #5, r4
StoreResult r4
TestFlags (allflags & ~(vf+cf+zf))
TestResult $FC252D28

;asr #31, $700FF00F: expect r4 = $0, nf, vf cleared, zf, cf set
LoadTestReg $700FF00F,r4
LoadFlags (nf+vf)
asr #31, r4
StoreResult r4
TestFlags cf+zf
TestResult 0

;asr #30, $3FFFFFFE: expect r4 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFE,r4
LoadFlags (vf+cf+nf)
asr #30, r4
StoreResult r4
TestFlags zf
TestResult 0

;asr #0, $80000000, r4: expect r4 = $80000001, vf, zf, cf cleared, nf set
LoadTestReg $80000000,r4
LoadFlags (allflags & ~nf)
asr #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf+cf))
TestResult $80000000

;asr #0, $80000001: expect r4 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadFlags (allflags & ~(nf+cf))
asr #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

test_lsl:
`test_lsl:

;lsl #m, Si, Sk

;lsl #5, $74A5A51E, r5: expect r5 = $94B4A3C0, zf, cf and vf cleared, nf set
SetTestNumber 56
LoadTestReg $74A5A51E,r4
LoadTestReg $0,r5
LoadFlags (allflags & ~nf)
lsl #5, r4, r5
StoreResult r5
TestFlags (allflags & ~(vf+cf+zf))
TestResult $94B4A3C0

;lsl #31, $F00FF00E, r5: expect r5 = $0, nf, vf cleared, zf, cf set
LoadTestReg $F00FF00E,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (vf+nf)
lsl #31, r4, r5
StoreResult r5
TestFlags cf+zf
TestResult 0

;lsl #30, $3FFFFFFC, r5: expect r5 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFC,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (vf+cf+nf)
lsl #30, r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;lsl #0, $80000000, r5: expect r5 = $80000000, vf, zf cleared, cf and nf set
LoadTestReg $80000000,r4
LoadTestReg $7FFFFFFE,r5
LoadFlags (allflags & ~(nf+cf))
lsl #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $80000000

;lsl #0, $80000001, r5: expect r5 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadTestReg $7FFFFFFE,r5
LoadFlags (allflags & ~(nf+cf))
lsl #0, r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

;lsl #m, Sk

;lsl #5, $74A5A51E: expect r4 = $94B4A3C0, zf, cf and vf cleared, nf set
SetTestNumber 57
LoadTestReg $74A5A51E,r4
LoadFlags (allflags & ~nf)
lsl #5, r4
StoreResult r4
TestFlags (allflags & ~(vf+cf+zf))
TestResult $94B4A3C0

;lsl #31, $F00FF00E: expect r4 = $0, nf, vf cleared, zf, cf set
LoadTestReg $F00FF00E,r4
LoadFlags (noflags)
lsl #31, r4
StoreResult r4
TestFlags cf+zf
TestResult 0

;lsl #30, $3FFFFFFC: expect r4 = $0, nf, cf, vf cleared, zf set
LoadTestReg $3FFFFFFC,r4
LoadFlags (noflags)
lsl #30, r4
StoreResult r4
TestFlags zf
TestResult 0

;lsl #0, $80000000, r4: expect r4 = $80000001, vf, zf cleared, cf, nf set
LoadTestReg $80000000,r4
LoadFlags (allflags)
lsl #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf))
TestResult $80000000

;lsl #0, $80000001: expect r4 = $4, vf, zf cleared, nf,cf set
LoadTestReg $80000001,r4
LoadFlags (allflags)
lsl #0, r4
StoreResult r4
TestFlags (allflags & ~(zf+vf))
TestResult $80000001

test_rot:
`test_rot:

;rot <>Sj, Si, Sk

;rot <>5, $84A5A51E, r6: expect r6 = $F4252D28, zf, vf cleared, nf set, cf unchanged
SetTestNumber 58
LoadTestReg 5,r4
LoadTestReg $84A5A51E,r5
LoadTestReg $0BDAD2D7,r6
LoadFlags (allflags & ~nf)
rot <>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(vf+zf))
TestResult $F4252D28

;rot <>31, $0FFFFF01, r6: expect r6 = $1FFFFE02, nf, zf, vf cleared, cf unchanged
LoadTestReg 31,r4
LoadTestReg $0FFFFF01,r5
LoadTestReg $E00001FD,r6
LoadFlags (nf+zf+vf)
rot <>r4, r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFE02

;rot <>-32, $7FFFFFFF, r6: expect r6 = $7FFFFFFF, nf, vf, zf cleared, cf unchanged
LoadTestReg -32,r4
LoadTestReg $7FFFFFFF,r5
LoadTestReg $80000000,r6
LoadFlags (nf+cf+vf+zf)
rot <>r4, r5, r6
StoreResult r6
TestFlags cf
TestResult $7FFFFFFF

;rot <>-31, $80000001, r6: expect r6 = $C0000000, vf, zf cleared, nf set, cf unchanged
LoadTestReg -31,r4
LoadTestReg $80000001,r5
LoadTestReg $3FFFFFFF,r6
LoadFlags (allflags & ~(cf+nf))
rot <>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(cf+zf+vf))
TestResult $C0000000

;rot <>-2, $80000001, r6: expect r6 = $6, vf, zf, nf cleared, cf unchanged
LoadTestReg -2,r4
LoadTestReg $80000001,r5
LoadTestReg $FFFFFFF9,r6
LoadFlags (allflags)
rot <>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf+nf))
TestResult $6

;rot <>0, $0, r6: expect r6 = $0, vf, nf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg $0,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (allflags & ~(zf+cf))
rot <>r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(vf+nf+cf))
TestResult $0

;rot #m, Si, Sk

;rot #5, $84A5A51E, r6: expect r6 = $F4252D28, zf, vf cleared, nf set, cf unchanged
SetTestNumber 59
LoadTestReg $84A5A51E,r5
LoadTestReg $0BDAD2D7,r6
LoadFlags (allflags & ~nf)
rot #5, r5, r6
StoreResult r6
TestFlags (allflags & ~(vf+zf))
TestResult $F4252D28

;rot #31, $0FFFFF01, r6: expect r6 = $1FFFFE02, nf, zf, vf cleared, cf unchanged
LoadTestReg $0FFFFF01,r5
LoadTestReg $E00001FD,r6
LoadFlags (nf+zf+vf)
rot #31, r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFE02

;rot #-32, $7FFFFFFF, r6: expect r6 = $7FFFFFFF, nf, vf, zf cleared, cf unchanged
LoadTestReg $7FFFFFFF,r5
LoadTestReg $80000000,r6
LoadFlags (nf+cf+vf+zf)
rot #-32, r5, r6
StoreResult r6
TestFlags cf
TestResult $7FFFFFFF

;rot #-31, $80000001, r6: expect r6 = $C0000000, vf, zf cleared, nf set, cf unchanged
LoadTestReg $80000001,r5
LoadTestReg $3FFFFFFF,r6
LoadFlags (allflags & ~(cf+nf))
rot #-31, r5, r6
StoreResult r6
TestFlags (allflags & ~(cf+zf+vf))
TestResult $C0000000

;rot #-2, $80000001, r6: expect r6 = $6, vf, zf, nf cleared, cf unchanged
LoadTestReg $80000001,r5
LoadTestReg $FFFFFFF9,r6
LoadFlags (allflags)
rot #-2, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf+nf))
TestResult $6

;rot #0, $0, r6: expect r6 = $0, vf, nf cleared, zf set, cf unchanged
LoadTestReg $0,r5
LoadTestReg $FFFFFFFF,r6
LoadFlags (allflags & ~(zf+cf))
rot #0, r5, r6
StoreResult r6
TestFlags (allflags & ~(vf+nf+cf))
TestResult $0

`test_mirror:

;mirror 0b00000001001000110100010101100111, r5
;expect r5 = 0b11100110101000101100010010000000, flags unchanged
SetTestNumber 60
LoadTestReg 0b00000001001000110100010101100111,r4
LoadTestReg 0b00011001010111010011101101111111,r5
LoadFlags allflags
mirror r4, r5
StoreResult r5
TestFlags allflags
TestResult 0b11100110101000101100010010000000

;mirror 0b11100110101000101100010010000000, r5
;expect r5 = 0b00000001001000110100010101100111, flags unchanged
LoadTestReg 0b11100110101000101100010010000000,r4
LoadTestReg 0b11111110110111001011101010011000,r5
LoadFlags noflags
mirror r4, r5
StoreResult r5
TestFlags noflags
TestResult 0b00000001001000110100010101100111

test_and:
`test_and:

;and Si, Sk
;r4 = $89ABCDEF, r5 = $FFFFFFFF, expect r5 = $89ABCDEF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 61
LoadTestReg $89ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
and r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDEF

;r4 = $00000000, r5 = $12345670, expect r5 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg $12345670,r5
LoadFlags nf+vf
and r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r5 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
and r4, r5
StoreResult r5
TestFlags noflags
TestResult $1000200C

;and Si, Sj, Sk
;r4 = $89ABCDEF, r5 = $FFFFFFFF, expect r6 = $89ABCDEF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 62
LoadTestReg $89ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
and r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDEF

;r4 = $00000000, r5 = $12345670, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg $12345670,r5
LoadFlags nf+vf
and r4, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
and r4, r5, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

;and #n, Sj, Sk
;n = -16, r5 = $FFFFFFFF, expect r6 = $FFFFFFF0, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 63
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
and #-16, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFF0

;n = 00000000, r5 = $12345670, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg $12345670,r5
LoadFlags nf+vf
and #0, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = 15, r5 = $5882300E, expect r6 = $E, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
and #15, r5, r6
StoreResult r6
TestFlags noflags
TestResult $E

;and #nnnn, Sj, Sk
;nnnn = $89ABCDEF, r5 = $FFFFFFFF, expect r6 = $89ABCDEF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 64
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
and #$89ABCDEF, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDEF

;nnnn = $A5A5FFFF, r5 = $5A5A0000, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg $5A5A0000,r5
LoadFlags nf+vf
and #$A5A5FFFF, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;nnnn = $3210ABCD, r5 = $5882300E, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
and #$3210ABCD, r5, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

;and #n, <>#m, Sk
;n = -3, m = -4, r6 = $89ABCDEF, expect r6 = $89ABCDCF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 65
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
and #-3, <>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDCF

;n = 15, m = 12, r6 = $FF0FFFFF, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg $FF0FFFFF,r6
LoadFlags nf+vf
and #15, <>#12, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = -1, m = 0, r6 = $1000200C, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
and #-1, <>#0, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

;and #n, >>Sj, Sk
;n = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $A55AA550, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 66
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
and #-3, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA550

;n = 15, r5 = 2, r6 = $C, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 2,r5
LoadTestReg $C,r6
LoadFlags nf+vf
and #15, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = -1, r5 = -28, r6 = $1FFFFFFF, expect r6 = $10000000, nf, zf and vf cleared, cf unchanged
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
and #-1, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $10000000

;and #nnnn, >>Sj, Sk
;nnnn = $F00FF00F, r5 = -16, r6 = $A55AA55A, expect r6 = $A00A0000, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 67
LoadTestReg -16,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
and #$FFFFF00F, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A00A0000

;nnnn = $A5A5FFFF, r5 = 16, r6 = $FFFF5A5A, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 16,r5
LoadTestReg $FFFF5A5A,r6
LoadFlags nf+vf
and #$A5A5FFFF, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;nnnn = $0F0FFFFF, r5 = -20, r6 = $1FFFFFFF, expect r6 = $1FF00000, nf, zf and vf cleared, cf unchanged
LoadTestReg -20,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
and #$0F0FFFFF, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FF00000

;and Si, >>Sj, Sk
;r4 = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $A55AA550, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 68
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
and r4, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA550

;r4 = 15, r5 = 2, r6 = $C, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 2,r5
LoadTestReg $C,r6
LoadFlags nf+vf
and r4, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -1, r5 = -28, r6 = $1FFFFFFF, expect r6 = $10000000, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
and r4, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $10000000

;and Si, >>#m, Sk
;r4 = -3, m = -4, r6 = $A55AA55A, expect r6 = $A55AA550, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 69
LoadTestReg -3,r4
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
and r4, >>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA550

;r4 = 15, m = 2, r6 = $C, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg $C,r6
LoadFlags nf+vf
and r4, >>#2, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -1, m = -16, r6 = $1FFFFFFF, expect r6 = $1FFF0000, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
and r4, >>#-16, r6
StoreResult r6
TestFlags noflags
TestResult $1FFF0000

;and Si, <>Sj, Sk
;r4 = -3, r5 = -4, r6 = $89ABCDEF, expect r6 = $89ABCDCF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 70
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
and r4, <>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDCF

;r4 = 15, r5 = 12, r6 = $FF0FFFFF, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 12,r5
LoadTestReg $FF0FFFFF,r6
LoadFlags nf+vf
and r4, <>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -1, r5 = 32, r6 = $1000200C, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg 32,r5
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
and r4, <>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

test_ftst:
`test_ftst:

;ftst Si, Sq
;r4 = $89ABCDEF, r5 = $FFFFFFFF, expect r5 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 71
LoadTestReg $89ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
ftst r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;r4 = $0, r5 = $12345670, expect r5 = $12345670, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg $12345670,r5
LoadFlags nf+vf
ftst r4, r5
StoreResult r5
TestFlags zf
TestResult $12345670

;r4 = $3210ABCD, r5 = $5882300E, expect r5 = $5882300E, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
ftst r4, r5
StoreResult r5
TestFlags noflags
TestResult $5882300E

;ftst #n, Sj
;n = -16, r5 = $FFFFFFFF, expect r5 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 72
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
ftst #-16, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;n = $0, r5 = $12345670, expect r5 = $12345670, nf and vf cleared, zf set, cf unchanged
LoadTestReg $12345670,r5
LoadFlags nf+vf
ftst #0, r5
StoreResult r5
TestFlags zf
TestResult $12345670

;n = 15, r5 = $5882300E, expect r5 = $5882300E, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
ftst #15, r5
StoreResult r5
TestFlags noflags
TestResult $5882300E

;ftst #nnnn, Sj
;nnnn = $89ABCDEF, r5 = $FFFFFFFF, expect r5 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 73
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
ftst #$89ABCDEF, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;nnnn = $A5A5FFFF, r5 = $5A5A0000, expect r5 = $5A5A0000, nf and vf cleared, zf set, cf unchanged
LoadTestReg $5A5A0000,r5
LoadFlags nf+vf
ftst #$A5A5FFFF, r5
StoreResult r5
TestFlags zf
TestResult $5A5A0000

;nnnn = $3210ABCD, r5 = $5882300E, expect r5 = $5882300E, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
ftst #$3210ABCD, r5
StoreResult r5
TestFlags noflags
TestResult $5882300E

;ftst #n, <>#m, Sq
;n = -3, m = -4, r6 = $89ABCDEF, expect r6 = $89ABCDEF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 74
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
ftst #-3, <>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDEF

;n = 15, m = 12, r6 = $FF0FFFFF, expect r6 = $FF0FFFFF, nf and vf cleared, zf set, cf unchanged
LoadTestReg $FF0FFFFF,r6
LoadFlags nf+vf
ftst #15, <>#12, r6
StoreResult r6
TestFlags zf
TestResult $FF0FFFFF

;n = -1, m = 0, r6 = $1000200C, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
ftst #-1, <>#0, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

;ftst #n, >>Sj, Sq
;n = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $A55AA55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 75
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
ftst #-3, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA55A

;n = 15, r5 = 2, r6 = $C, expect r6 = $C, nf and vf cleared, zf set, cf unchanged
LoadTestReg 2,r5
LoadTestReg $C,r6
LoadFlags nf+vf
ftst #15, >>r5, r6
StoreResult r6
TestFlags zf
TestResult $C

;n = -1, r5 = -28, r6 = $1FFFFFFF, expect r6 = $1FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
ftst #-1, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFFFF

;ftst #nnnn, >>Sj, Sq
;nnnn = $F00FF00F, r5 = -16, r6 = $A55AA55A, expect r6 = $A55AA55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 76
LoadTestReg -16,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
ftst #$FFFFF00F, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA55A

;nnnn = $A5A5FFFF, r5 = 16, r6 = $FFFF5A5A, expect r6 = $FFFF5A5A, nf and vf cleared, zf set, cf unchanged
LoadTestReg 16,r5
LoadTestReg $FFFF5A5A,r6
LoadFlags nf+vf
ftst #$A5A5FFFF, >>r5, r6
StoreResult r6
TestFlags zf
TestResult $FFFF5A5A

;nnnn = $0F0FFFFF, r5 = -20, r6 = $1FFFFFFF, expect r6 = $1FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -20,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
ftst #$0F0FFFFF, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFFFF

;ftst Si, >>Sj, Sk
;r4 = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $A55AA55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 77
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
ftst r4, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA55A

;r4 = 15, r5 = 2, r6 = $C, expect r6 = $C, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 2,r5
LoadTestReg $C,r6
LoadFlags nf+vf
ftst r4, >>r5, r6
StoreResult r6
TestFlags zf
TestResult $C

;r4 = -1, r5 = -28, r6 = $1FFFFFFF, expect r6 = $1FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
ftst r4, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFFFF

;ftst Si, >>#m, Sk
;r4 = -3, m = -4, r6 = $A55AA55A, expect r6 = $A55AA55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 78
LoadTestReg -3,r4
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
ftst r4, >>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $A55AA55A

;r4 = 15, m = 2, r6 = $C, expect r6 = $C, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg $C,r6
LoadFlags nf+vf
ftst r4, >>#2, r6
StoreResult r6
TestFlags zf
TestResult $C

;r4 = -1, m = -16, r6 = $1FFFFFFF, expect r6 = $1FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
ftst r4, >>#-16, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFFFF

;ftst Si, <>Sj, Sq
;r4 = -3, r5 = -4, r6 = $89ABCDEF, expect r6 = $89ABCDEF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 79
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
ftst r4, <>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $89ABCDEF

;r4 = 15, r5 = 12, r6 = $FF0FFFFF, expect r6 = $FF0FFFFF, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 12,r5
LoadTestReg $FF0FFFFF,r6
LoadFlags nf+vf
ftst r4, <>r5, r6
StoreResult r6
TestFlags zf
TestResult $FF0FFFFF

;r4 = -1, r5 = 32, r6 = $1000200C, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg 32,r5
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
ftst r4, <>r5, r6
StoreResult r6
TestFlags noflags
TestResult $1000200C

test_btst:
`test_btst:

;btst #m, Sj

;Nuon hardware bug!  In the case of BTST #31, $80000000, the negative flag is
;not set correctly.

;m = 31, r6 = $80000000, expect r6 = $80000000, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 80
LoadTestReg $80000000,r6
LoadFlags (allflags & ~nf)
btst #31, r6
StoreResult r6
;TestFlags (allflags & ~(zf+vf))
TestFlags (allflags & ~(nf+zf+vf))
TestResult $80000000

;m = 6, $FFFFFFBF, expect r6 = $FFFFFFBF, nf and vf cleared, zf set, cf unchanged
LoadTestReg $FFFFFFBF,r6
LoadFlags nf+vf
btst #6, r6
StoreResult r6
TestFlags zf
;TestFlags nf+zf
TestResult $FFFFFFBF

;m = 3, r6 = $00000008, expect r6 = $00000008, nf, zf and vf cleared, cf unchanged
LoadTestReg $8,r6
LoadFlags nf+vf+zf
btst #3,r6
StoreResult r6
TestFlags noflags
;TestFlags nf
TestResult $8

;Test to confirm Nuon hardware bug.  This test shows that BTST #31, Sk, with
;Sk containing _any_ negative number, will always cause the negative flag to be
;cleared

;m = 31, $F00FF00F, expect r6 = $F00FF00F, nf cleared, vf, zf  cleared, cf unchanged
LoadTestReg $F00FF00F,r6
LoadFlags nf+vf+zf
btst #31, r6
StoreResult r6
TestFlags noflags
TestResult $F00FF00F

;Test to make sure there is no bug with btst #31 when testing a positive number
;m = 31, $7FFFFFFF, expect r6 = $7FFFFFFF, nf, vf, zf  cleared, cf unchanged
LoadTestReg $7FFFFFFF,r6
LoadFlags nf+vf
btst #31, r6
StoreResult r6
TestFlags zf
TestResult $7FFFFFFF

`test_or:

;or Si, Sk
;r4 = $89ABCDEF, r5 = $FFFFFFFF, expect r5 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 81
LoadTestReg $89ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
or r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;r4 = $00000000, r5 = $00000000, expect r5 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg 0,r5
LoadFlags nf+vf
or r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r5 = $7A92BBCF, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
or r4, r5
StoreResult r5
TestFlags noflags
TestResult $7A92BBCF

;or Si, Sj, Sk
;r4 = $89ABCDEF, r5 = $FFFFFFFF, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 82
LoadTestReg $89ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
or r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;r4 = $00000000, r5 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r4
LoadTestReg 0,r5
LoadFlags nf+vf
or r4, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r6 = $1000200C, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
or r4, r5, r6
StoreResult r6
TestFlags noflags
TestResult $7A92BBCF

;or #n, Sj, Sk
;n = -16, r5 = $FFFFFFFF, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 83
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
or #-16, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;n = 00000000, r5 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r5
LoadFlags nf+vf
or #0, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = 15, r5 = $5882300E, expect r6 = $5882300F, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
or #15, r5, r6
StoreResult r6
TestFlags noflags
TestResult $5882300F

;or #nnnn, Sj, Sk
;nnnn = $89ABCDEF, r5 = $FFFFFFFF, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 84
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
or #$89ABCDEF, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;nnnn = $3210ABCD, r5 = $5882300E, expect r6 = $7A92BBCF, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
or #$3210ABCD, r5, r6
StoreResult r6
TestFlags noflags
TestResult $7A92BBCF

;or #n, <>#m, Sk
;n = -3, m = -4, r6 = $89ABCDEF, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 85
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
or #-3, <>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;n = 15, m = 12, r6 = $FF0FFFFF, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg $FF0FFFFF,r6
LoadFlags zf+vf
or #15, <>#12, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

;n = 3, m = 0, r6 = $1000200C, expect r6 = $1000200F, nf, zf and vf cleared, cf unchanged
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
or #3, <>#0, r6
StoreResult r6
TestFlags noflags
TestResult $1000200F

;or #n, >>Sj, Sk
;n = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $FFFFFFDA, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 86
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
or #-3, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFDA

;n = 15, r5 = 4, r6 = $0, expect r6 = $0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 4,r5
LoadTestReg $0,r6
LoadFlags nf+vf
or #15, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = -9, r5 = -28, r6 = $1FFFFFFF, expect r6 = $7FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
or #-9, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $7FFFFFFF

;or #nnnn, >>Sj, Sk
;nnnn = $FFFFF00F, r5 = -16, r6 = $A55AA55A, expect r6 = $F55FA55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 87
LoadTestReg -16,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
or #$FFFFF00F, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $F55FA55A

;nnnn = $0000FFFF, r5 = 16, r6 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 16,r5
LoadTestReg 0,r6
LoadFlags nf+vf
or #$0000FFFF, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;nnnn = $0F0FF7FF, r5 = -20, r6 = $1FF00000, expect r6 = $7FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -20,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
or #$0F0FF7FF, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $7FFFFFFF

;or Si, >>Sj, Sk
;r4 = -3, r5 = -4, r6 = $A55AA55A, expect r6 = $FFFFFFDA, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 88
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
or r4, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFDA

;r4 = 15, r5 = 4, r6 = $0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 4,r5
LoadTestReg $0,r6
LoadFlags nf+vf
or r4, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -9, r5 = -28, r6 = $1FFFFFFF, expect r6 = $7FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -9,r4
LoadTestReg -28,r5
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
or r4, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $7FFFFFFF

;or Si, >>#m, Sk
;r4 = -3, m = -4, r6 = $A55AA55A, expect r6 = $FFFFFFDA, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 89
LoadTestReg -3,r4
LoadTestReg $A55AA55A,r6
LoadFlags (allflags & ~nf)
or r4, >>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFDA

;r4 = 15, m = 4, r6 = $0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 0,r6
LoadFlags nf+vf
or r4, >>#4, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -1, m = 3, r6 = $1FFFFFFF, expect r6 = $1FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg $1FFFFFFF,r6
LoadFlags nf+vf+zf
or r4, >>#3, r6
StoreResult r6
TestFlags noflags
TestResult $1FFFFFFF

;or Si, <>Sj, Sk
;r4 = -3, r5 = -4, r6 = $89ABCDEF, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 90
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $89ABCDEF,r6
LoadFlags (allflags & ~nf)
or r4, <>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;r4 = 15, r5 = 12, r6 = $FF0FFFFF, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 12,r5
LoadTestReg $FF0FFFFF,r6
LoadFlags zf+vf
or r4, <>r5, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

;r4 = -2, r5 = 32, r6 = $1, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg -2,r4
LoadTestReg 32,r5
LoadTestReg $1,r6
LoadFlags vf+zf
or r4, <>r5, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

`test_eor:

;eor Si, Sk
;r4 = $79ABCDEF, r5 = $FFFFFFFF, expect r5 = $86543210, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 91
LoadTestReg $79ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
eor r4, r5
StoreResult r5
TestFlags (allflags & ~(zf+vf))
TestResult $86543210

;r4 = $FFFFFFFF, r5 = $FFFFFFFF, expect r5 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg $FFFFFFFF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags nf+vf
eor r4, r5
StoreResult r5
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r5 = $6A929BC3, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
eor r4, r5
StoreResult r5
TestFlags noflags
TestResult $6A929BC3

;eor Si, Sj, Sk
;r4 = $79ABCDEF, r5 = $FFFFFFFF, expect r6 = $86543210, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 92
LoadTestReg $79ABCDEF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
eor r4, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $86543210

;r4 = $FFFFFFFF, r5 = $FFFFFFFF, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg $FFFFFFFF,r4
LoadTestReg $FFFFFFFF,r5
LoadFlags nf+vf
eor r4, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = $3210ABCD, r5 = $5882300E, expect r6 = $6A929BC3, nf, zf and vf cleared, cf unchanged
LoadTestReg $3210ABCD,r4
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
eor r4, r5, r6
StoreResult r6
TestFlags noflags
TestResult $6A929BC3

;eor #n, Sk
;n = -16, r6 = $F, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 93
LoadTestReg $F,r6
LoadFlags (allflags & ~nf)
eor #-16, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;n = 00000000, r6 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r6
LoadFlags nf+vf
eor #0, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = 15, r6 = $5882300E, expect r6 = $58823001, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r6
LoadFlags nf+vf+zf
eor #15, r6
StoreResult r6
TestFlags noflags
TestResult $58823001

;eor #n, Sj, Sk
;n = -16, r5 = $F, expect r6 = $FFFFFFFF, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 94
LoadTestReg $F,r5
LoadFlags (allflags & ~nf)
eor #-16, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $FFFFFFFF

;n = 00000000, r5 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 0,r5
LoadFlags nf+vf
eor #0, r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = 15, r5 = $5882300E, expect r6 = $58823001, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
eor #15, r5, r6
StoreResult r6
TestFlags noflags
TestResult $58823001

;eor #nnnn, Sk
;nnnn = $79ABCDEF, r6 = $FFFFFFFF, expect r6 = $86543210, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 95
LoadTestReg $FFFFFFFF,r6
LoadFlags (allflags & ~nf)
eor #$79ABCDEF, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $86543210

;nnnn = $3210ABCD, r5 = $5882300E, expect r6 = $6A929BC3, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r6
LoadFlags nf+vf+zf
eor #$3210ABCD, r6
StoreResult r6
TestFlags noflags
TestResult $6A929BC3

;eor #nnnn, Sj, Sk
;nnnn = $79ABCDEF, r5 = $FFFFFFFF, expect r6 = $86543210, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 96
LoadTestReg $FFFFFFFF,r5
LoadFlags (allflags & ~nf)
eor #$79ABCDEF, r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $86543210

;nnnn = $3210ABCD, r5 = $5882300E, expect r6 = $6A929BC3, nf, zf and vf cleared, cf unchanged
LoadTestReg $5882300E,r5
LoadFlags nf+vf+zf
eor #$3210ABCD, r5, r6
StoreResult r6
TestFlags noflags
TestResult $6A929BC3

;eor #n, <>#m, Sk
;n = -3, m = -4, r6 = $79ABCDEF, expect r6 = $86543230, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 97
LoadTestReg $79ABCDEF,r6
LoadFlags (allflags & ~nf)
eor #-3, <>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $86543230

;n = 15, m = 12, r6 = $FF0FFFFF, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg $FF0FFFFF,r6
LoadFlags zf+vf
eor #15, <>#12, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

;n = 3, m = 0, r6 = $1000200C, expect r6 = $1000200F, nf, zf and vf cleared, cf unchanged
LoadTestReg $1000200C,r6
LoadFlags nf+vf+zf
eor #3, <>#0, r6
StoreResult r6
TestFlags noflags
TestResult $1000200F

;eor #n, >>Sj, Sk
;n = -3, r5 = -4, r6 = $755AA55A, expect r6 = $8AA55A8A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 98
LoadTestReg -4,r5
LoadTestReg $755AA55A,r6
LoadFlags (allflags & ~nf)
eor #-3, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $8AA55A8A

;n = 15, r5 = 4, r6 = $0, expect r6 = $0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 4,r5
LoadTestReg $0,r6
LoadFlags nf+vf
eor #15, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;n = -9, r5 = -28, r6 = $0FFFFFFF, expect r6 = $7FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -28,r5
LoadTestReg $0FFFFFFF,r6
LoadFlags nf+vf+zf
eor #-9, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $7FFFFFFF

;eor #nnnn, >>Sj, Sk
;nnnn = $FFFFF00F, r5 = -16, r6 = $755AA55A, expect r6 = $8555A55A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 99
LoadTestReg -16,r5
LoadTestReg $755AA55A,r6
LoadFlags (allflags & ~nf)
eor #$FFFFF00F, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $8555A55A

;nnnn = $0000FFFF, r5 = 16, r6 = 0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 16,r5
LoadTestReg 0,r6
LoadFlags nf+vf
eor #$0000FFFF, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;nnnn = $0F0FF7FF, r5 = -20, r6 = $0FFFFFFF, expect r6 = $700FFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -20,r5
LoadTestReg $0FFFFFFF,r6
LoadFlags nf+vf+zf
eor #$0F0FF7FF, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $700FFFFF

;eor Si, >>Sj, Sk
;r4 = -3, r5 = -4, r6 = $755AA55A, expect r6 = $8AA55A8A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 100
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $755AA55A,r6
LoadFlags (allflags & ~nf)
eor r4, >>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $8AA55A8A

;r4 = 15, r5 = 4, r6 = $0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 4,r5
LoadTestReg $0,r6
LoadFlags nf+vf
eor r4, >>r5, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -9, r5 = -28, r6 = $0FFFFFFF, expect r6 = $7FFFFFFF, nf, zf and vf cleared, cf unchanged
LoadTestReg -9,r4
LoadTestReg -28,r5
LoadTestReg $0FFFFFFF,r6
LoadFlags nf+vf+zf
eor r4, >>r5, r6
StoreResult r6
TestFlags noflags
TestResult $7FFFFFFF

;eor Si, >>#m, Sk
;r4 = -3, m = -4, r6 = $755AA55A, expect r6 = $8AA55A8A, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 101
LoadTestReg -3,r4
LoadTestReg $755AA55A,r6
LoadFlags (allflags & ~nf)
eor r4, >>#-4, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $8AA55A8A

;r4 = 15, m = 4, r6 = $0, expect r6 = 0, nf and vf cleared, zf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 0,r6
LoadFlags nf+vf
eor r4, >>#4, r6
StoreResult r6
TestFlags zf
TestResult 0

;r4 = -1, m = 3, r6 = $7FFFFFFF, expect r6 = $60000000, nf, zf and vf cleared, cf unchanged
LoadTestReg -1,r4
LoadTestReg $7FFFFFFF,r6
LoadFlags nf+vf+zf
eor r4, >>#3, r6
StoreResult r6
TestFlags noflags
TestResult $60000000

;eor Si, <>Sj, Sk
;r4 = -3, r5 = -4, r6 = $79ABCDEF, expect r6 = $86543230, nf set, zf amd vf cleared, cf unchanged
SetTestNumber 102
LoadTestReg -3,r4
LoadTestReg -4,r5
LoadTestReg $79ABCDEF,r6
LoadFlags (allflags & ~nf)
eor r4, <>r5, r6
StoreResult r6
TestFlags (allflags & ~(zf+vf))
TestResult $86543230

;r4 = 15, r5 = 12, r6 = $FF0FFFFF, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg 15,r4
LoadTestReg 12,r5
LoadTestReg $FF0FFFFF,r6
LoadFlags zf+vf
eor r4, <>r5, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

;r4 = -2, r5 = 32, r6 = $1, expect r6 = $FFFFFFFF, zf and vf cleared, nf set, cf unchanged
LoadTestReg -2,r4
LoadTestReg 32,r5
LoadTestReg $1,r6
LoadFlags vf+zf
eor r4, <>r5, r6
StoreResult r6
TestFlags nf
TestResult $FFFFFFFF

;==================================================================
; WC (with-carry) zero-flag preservation tests
;==================================================================
; Per "MPE Instruction Set Reference" (addwc,
; subwc, cmpwc): the WC variants document Z as
;   "z : unchanged if the result is zero, cleared otherwise."
; This is what makes multi-precision equality compares work:
;   cmp lo_a, lo_b ; cmpwc hi_a, hi_b
; chains the per-word match into Z - Z stays 1 only if every word
; matched. Any earlier word disagreeing must clear Z permanently.
;
; The original emulator implementation force-cleared Z, then set it
; based purely on the current word - wrong, the chain would falsely
; report equality whenever just the high word matched. Fixed nowadays

`test_addwc_zflag:

;ADDWC: result == 0 with prior Z=1 ? Z must be PRESERVED (stays 1)
;src1=$FFFFFFFE, src2=1, carry-in=1 (from CC.C) ? 1 + $FFFFFFFE + 1 = $100000000 ? 0 + carry-out
SetTestNumber 103
LoadTestReg 1, r4
LoadFlags zf+cf
addwc #$FFFFFFFE, r4
StoreResult r4
TestFlagsExact zf+cf      ; Z preserved (1), C set (carry out), N=0, V=0
TestResult 0

;ADDWC: result != 0 with prior Z=1 ? Z must be CLEARED
;src1=$FFFFFFFD, src2=1, carry-in=1 ? 1 + $FFFFFFFD + 1 = $FFFFFFFF (no carry, negative)
SetTestNumber 104
LoadTestReg 1, r4
LoadFlags zf+cf
addwc #$FFFFFFFD, r4
StoreResult r4
TestFlagsExact nf         ; Z cleared, N set ($FFFFFFFF is negative), V=0, C=0
TestResult $FFFFFFFF

;ADDWC: result == 0 with prior Z=0 ? Z STAYS 0 (no spurious set)
SetTestNumber 105
LoadTestReg 1, r4
LoadFlags cf              ; Z=0 (cleared), C=1 (carry-in)
addwc #$FFFFFFFE, r4      ; same as test 103 inputs but Z initially 0
StoreResult r4
TestFlagsExact cf         ; Z stays 0, C set
TestResult 0

`test_cmpwc_zflag:

; cmp src1,src2 computes src2 - src1.  Sets C=1 if borrow needed, else C=0.
; cmpwc src1,src2 computes src2 - src1 - borrow_in, where
;   borrow_in = (tempCC & CC_ALU_CARRY) >> 1  i.e. C from the prior compare.

;CMPWC: chained 64-bit equality compare where both halves match ? Z stays 1
;Compare $1234567887654321 with $1234567887654321
SetTestNumber 106
LoadTestReg $87654321, r4
LoadTestReg $12345678, r5
LoadTestReg $87654321, r6
LoadTestReg $12345678, r7
cmp r4, r6                ; r6 - r4 = 0, Z=1, C=0 (no borrow)
cmpwc r5, r7              ; r7 - r5 - 0 = 0; Z must stay 1 (preserved from cmp)
TestFlagsExact zf         ; full 64-bit equality reported

;CMPWC: low halves DIFFER (r6 > r4), high halves match ? Z must be 0 (was the buggy case)
;Compare $1234567887654322 with $1234567887654321
SetTestNumber 107
LoadTestReg $87654321, r4
LoadTestReg $12345678, r5
LoadTestReg $87654322, r6
LoadTestReg $12345678, r7
cmp r4, r6                ; r6 - r4 = 1, Z=0, C=0 (no borrow)
cmpwc r5, r7              ; r7 - r5 - 0 = 0 (this word equal); buggy emulator force-sets Z
TestFlagsExact 0          ; Z must STAY 0 - full 64-bit values differ

;CMPWC: low halves match, high halves DIFFER (r7 > r5) ? Z must be 0
;Compare $1234567987654321 with $1234567887654321
SetTestNumber 108
LoadTestReg $87654321, r4
LoadTestReg $12345678, r5
LoadTestReg $87654321, r6
LoadTestReg $12345679, r7
cmp r4, r6                ; equal, Z=1, C=0
cmpwc r5, r7              ; r7 - r5 - 0 = 1, non-zero ? Z cleared
TestFlagsExact 0          ; Z cleared, C=0 (no borrow), N=0, V=0

`test_subwc_zflag:

;SUBWC: result == 0 with prior Z=1, borrow_in=1 ? Z preserved (stays 1)
;Pick: src2=2, src1=1, borrow_in=1 ? 2 - 1 - 1 = 0
SetTestNumber 109
LoadTestReg 2, r4
LoadFlags zf+cf           ; Z=1, C=1 ? tempCC.C=1 ? borrow_in=1
subwc #1, r4              ; r4 = 2 - 1 - 1 = 0
StoreResult r4
TestFlagsExact zf         ; Z preserved (was 1, result is 0), C=0, V=0, N=0
TestResult 0

;SUBWC: result != 0 with prior Z=1, borrow_in=0 ? Z cleared
SetTestNumber 110
LoadTestReg 5, r4
LoadFlags zf              ; Z=1, C=0 ? borrow_in = 0
subwc #1, r4              ; r4 = 5 - 1 - 0 = 4
StoreResult r4
TestFlagsExact 0          ; Z cleared, C=0 (no borrow), N=0, V=0
TestResult 4

;==================================================================
; Plain ALU add/sub/cmp corner cases - flag corners of the basic
; 3-operand forms. Direct flag-affecting add/sub/cmp tests
; (previously only addm/subm, which are multiply-unit variants
; existed that don't touch flags). Style: LoadFlags noflags + small-list
; TestFlags, matching test_abs / test_neg / test_btst.
;
; Convention recap (from src/ExecuteALU.cpp):
;   add  Si,Sj,Sk : Sk = Si + Sj
;   sub  Si,Sj,Sk : Sk = Sj - Si    (Si subtracted FROM Sj)
;   cmp  Si,Sj    : computes Sj - Si, sets flags only
;==================================================================

`test_add_corners:

;ADD: $7FFFFFFF + 1 = $80000000 - signed overflow.
;Result high bit set ? N=1; pos+pos=neg ? V=1; no carry ? C=0; non-zero ? Z=0
SetTestNumber 111
LoadTestReg $7FFFFFFF, r4
LoadTestReg 1, r5
LoadTestReg 0, r6
LoadFlags noflags
add r4, r5, r6
StoreResult r6
TestFlags nf+vf
TestResult $80000000

;ADD: $FFFFFFFF + 1 = 0 - unsigned wrap, zero result + carry.
;Z=1, C=1, N=0, V=0
SetTestNumber 112
LoadTestReg $FFFFFFFF, r4
LoadTestReg 1, r5
LoadTestReg $DEADBEEF, r6
LoadFlags noflags
add r4, r5, r6
StoreResult r6
TestFlags zf+cf
TestResult 0

;ADD: $FFFFFFFF + $FFFFFFFF = $FFFFFFFE - both negative as signed, both wrap unsigned.
;Result high bit set ? N=1; carry out ? C=1; -1 + -1 = -2 fits, no signed overflow ? V=0; non-zero ? Z=0
SetTestNumber 113
LoadTestReg $FFFFFFFF, r4
LoadTestReg $FFFFFFFF, r5
LoadTestReg 0, r6
LoadFlags noflags
add r4, r5, r6
StoreResult r6
TestFlags nf+cf
TestResult $FFFFFFFE

;ADD: $7FFFFFFF + $7FFFFFFF = $FFFFFFFE - both positive, signed overflow.
;N=1, V=1 (sign of result wrong), C=0 (no unsigned carry), Z=0
SetTestNumber 114
LoadTestReg $7FFFFFFF, r4
LoadTestReg $7FFFFFFF, r5
LoadTestReg 0, r6
LoadFlags noflags
add r4, r5, r6
StoreResult r6
TestFlags nf+vf
TestResult $FFFFFFFE

`test_sub_corners:

;SUB equal: $12345678 - $12345678 = 0.  Z set, no other ALU flags.
SetTestNumber 115
LoadTestReg $12345678, r4
LoadTestReg $12345678, r5
LoadTestReg $DEADBEEF, r6
LoadFlags noflags
sub r4, r5, r6        ; r6 = r5 - r4 = 0
StoreResult r6
TestFlags zf
TestResult 0

;SUB: 0 - 1 = $FFFFFFFF - unsigned underflow with borrow.
;N=1 (high bit), C=1 (borrow), V=0 (-1 fits), Z=0
SetTestNumber 116
LoadTestReg 1, r4
LoadTestReg 0, r5
LoadTestReg 0, r6
LoadFlags noflags
sub r4, r5, r6
StoreResult r6
TestFlags nf+cf
TestResult $FFFFFFFF

;SUB: $80000000 - 1 = $7FFFFFFF - signed overflow (MIN_INT - positive).
;N=0, V=1, C=0 (no unsigned underflow: $80000000 > 1), Z=0
SetTestNumber 117
LoadTestReg 1, r4
LoadTestReg $80000000, r5
LoadTestReg 0, r6
LoadFlags noflags
sub r4, r5, r6
StoreResult r6
TestFlags vf
TestResult $7FFFFFFF

`test_cmp_corners:

;CMP equal: Z=1, no other ALU flags.
SetTestNumber 118
LoadTestReg $12345678, r4
LoadTestReg $12345678, r5
LoadFlags noflags
cmp r4, r5            ; computes r5 - r4 = 0
TestFlags zf

;CMP r5 (4) < r4 (5):  r5 - r4 = -1 = $FFFFFFFF.
;N=1, C=1 (borrow), V=0, Z=0
SetTestNumber 119
LoadTestReg 5, r4
LoadTestReg 4, r5
LoadFlags noflags
cmp r4, r5
TestFlags nf+cf

;CMP signed-overflow: cmp $80000000, $7FFFFFFF.  r5 - r4 = $FFFFFFFF (modular).
;N=1, V=1 (MAX - MIN: signed result -1 is wrong, expected positive), C=1, Z=0
SetTestNumber 120
LoadTestReg $80000000, r4
LoadTestReg $7FFFFFFF, r5
LoadFlags noflags
cmp r4, r5
TestFlags nf+cf+vf

`test_shift_corners:

;ASL #0 of $80000000 - NUON treats #0 as a special case that sets C from
;the high bit (matches existing lsl #0 test at line 1521 expecting cf+nf).
;Result = $80000000, N=1, C=1, Z=0, V=0.
SetTestNumber 121
LoadTestReg $80000000, r4
LoadTestReg 0, r5
LoadFlags noflags
asl #0, r4, r5
StoreResult r5
TestFlags nf+cf
TestResult $80000000

;ASR #31 of $80000000 - fully sign-extends to $FFFFFFFF.
;N=1 (result still has high bit), Z=0
SetTestNumber 122
LoadTestReg $80000000, r4
LoadTestReg 0, r5
LoadFlags noflags
asr #31, r4, r5
StoreResult r5
TestFlags nf
TestResult $FFFFFFFF

;LSR #31 of $FFFFFFFF - only bit 0 of result set.
;Result = 1.  Per OEM spec: "c : bit 0 of source A" - always, for any
;shift count.  bit 0 of $FFFFFFFF = 1 ? C=1.  N=0, Z=0, V=0.
SetTestNumber 123
LoadTestReg $FFFFFFFF, r4
LoadTestReg 0, r5
LoadFlags noflags
lsr #31, r4, r5
StoreResult r5
TestFlags cf
TestResult 1

;==================================================================
; MUL scalar-integer multiplication
;
; Form used here: mul Si, Sk, >>#m, Sk
;   - 32x32 signed multiply, full 64-bit product
;   - product is arithmetically shifted by #m (-32..+63; positive = right)
;   - bottom 32 bits of shifted product written to Sk
; Condition codes:
;   mv : set if there are significant two's-complement bits above the
;        extracted 32-bit result (i.e., signed overflow), cleared otherwise.
;   Other condition codes are unchanged.
;==================================================================

`test_mul_corners:

;MUL: 5 * 6 = 30, no shift, no overflow.
;Result fits in 32 bits ? mv cleared. Other flags unchanged.
;NUON multiply has a 2-cycle latency - needs a nop before reading the result.
SetTestNumber 124
LoadTestReg 5, r4
LoadTestReg 6, r5
LoadFlags mvf            ; pre-set mv to verify it gets cleared
mul r4, r5, >>#0, r5
nop                      ; mul result not ready until +2 cycles
StoreResult r5
TestFlags noflags        ; mv cleared, others stayed at 0
TestResult 30

;MUL: $7FFFFFFF * $7FFFFFFF - overflow.
;Product = 0x3FFFFFFF00000001 ? low 32 = 0x00000001, high 32 = 0x3FFFFFFF (significant) ? mv set.
SetTestNumber 125
LoadTestReg $7FFFFFFF, r4
LoadTestReg $7FFFFFFF, r5
LoadFlags noflags
mul r4, r5, >>#0, r5
nop
StoreResult r5
TestFlags mvf            ; mv set (overflow); others preserved at 0
TestResult $00000001

;MUL: -1 * -1 = 1, no overflow.
;64-bit product = 0x0000000000000001, no significant high bits ? mv cleared.
SetTestNumber 126
LoadTestReg $FFFFFFFF, r4
LoadTestReg $FFFFFFFF, r5
LoadFlags noflags
mul r4, r5, >>#0, r5
nop
StoreResult r5
TestFlags noflags
TestResult 1

;MUL: 30 * 1 with right shift by 1 = 15 (test the shift amount).
SetTestNumber 127
LoadTestReg 30, r4
LoadTestReg 1, r5
LoadFlags noflags
mul r4, r5, >>#1, r5
nop
StoreResult r5
TestFlags noflags
TestResult 15

;==================================================================
; DOTP small-vector dot product
;
; Form used here: dotp Vi, Vj, >>#m, Sk
;   - small vector = upper 16 bits of each of 4 scalars in v_n register
;     (v1 = r4..r7, v2 = r8..r11, v3 = r12..r15)
;   - Four 16x16 signed multiplies, summed (no overflow detection in sum)
;   - Sum shifted by #m, encoded as: 16=shl-by-16, 24=shl-by-8,
;     32=no-shift, 30=shl-by-2.
; Condition codes: unchanged by this instruction.
;==================================================================

`test_dotp:

;DOTP basic positive: v1=[1,2,3,4] dot v2=[5,6,7,8], no shift.
;= 1*5 + 2*6 + 3*7 + 4*8 = 5+12+21+32 = 70 = 0x46
;dotp also has 2-cycle latency - same nop discipline.
SetTestNumber 128
LoadTestReg $00010000, r4   ; v1[0] = 1
LoadTestReg $00020000, r5   ; v1[1] = 2
LoadTestReg $00030000, r6   ; v1[2] = 3
LoadTestReg $00040000, r7   ; v1[3] = 4
LoadTestReg $00050000, r8   ; v2[0] = 5
LoadTestReg $00060000, r9   ; v2[1] = 6
LoadTestReg $00070000, r10  ; v2[2] = 7
LoadTestReg $00080000, r11  ; v2[3] = 8
LoadTestReg $DEADBEEF, r12  ; sentinel
LoadFlags allflags          ; verify dotp doesn't touch ANY flag
dotp v1, v2, >>#32, r12
nop
StoreResult r12
TestFlags allflags          ; flags unchanged
TestResult 70

;DOTP with #16 shift (shift left by 16): same vectors, result = 70 << 16 = 0x00460000
SetTestNumber 129
LoadTestReg $00010000, r4
LoadTestReg $00020000, r5
LoadTestReg $00030000, r6
LoadTestReg $00040000, r7
LoadTestReg $00050000, r8
LoadTestReg $00060000, r9
LoadTestReg $00070000, r10
LoadTestReg $00080000, r11
LoadTestReg 0, r12
LoadFlags noflags
dotp v1, v2, >>#16, r12
nop
StoreResult r12
TestFlags noflags
TestResult $00460000

;DOTP with negatives: v1=[-1,2,-3,4] dot v2=[5,-6,7,8], no shift.
;= (-1*5) + (2*-6) + (-3*7) + (4*8) = -5 -12 -21 +32 = -6 = $FFFFFFFA (32-bit signed)
SetTestNumber 130
LoadTestReg $FFFF0000, r4   ; v1[0] = -1 (16-bit signed)
LoadTestReg $00020000, r5   ; v1[1] = 2
LoadTestReg $FFFD0000, r6   ; v1[2] = -3
LoadTestReg $00040000, r7   ; v1[3] = 4
LoadTestReg $00050000, r8   ; v2[0] = 5
LoadTestReg $FFFA0000, r9   ; v2[1] = -6
LoadTestReg $00070000, r10  ; v2[2] = 7
LoadTestReg $00080000, r11  ; v2[3] = 8
LoadTestReg 0, r12
LoadFlags noflags
dotp v1, v2, >>#32, r12
nop
StoreResult r12
TestFlags noflags
TestResult $FFFFFFFA

;DOTP scalar form: dotp Si, Vj, >>#m, Sk
;Si's upper 16 bits get repeated 4× to form a small vector.
;Si = r4 with upper-16 = 2, so vector = [2,2,2,2].  Vj = v2 = [5,-6,7,8].
;= 2*5 + 2*-6 + 2*7 + 2*8 = 10-12+14+16 = 28 = 0x1C
SetTestNumber 131
LoadTestReg $0002BEEF, r4   ; upper 16 bits = 2; lower ignored
LoadTestReg $00050000, r8
LoadTestReg $FFFA0000, r9
LoadTestReg $00070000, r10
LoadTestReg $00080000, r11
LoadTestReg 0, r12
LoadFlags noflags
dotp r4, v2, >>#32, r12
nop
StoreResult r12
TestFlags noflags
TestResult 28

;==================================================================
; BUTT - butterfly operation
;
; butt Si, Sj, Hk: writes (Sj+Si) to Hk, (Sj-Si) to Hk+1.
; Hk must be an even-numbered register; the half-vector is (Hk, Hk+1).
; Condition codes set per the ADD result (sum):
;   z, n, c, v based on Sj+Si.  Other bits unchanged.
;==================================================================

`test_butt:

;BUTT basic: 10 + 3 = 13, 10 - 3 = 7. No flags special.
SetTestNumber 132
LoadTestReg 3, r4         ; Si
LoadTestReg 10, r5        ; Sj
LoadTestReg 0, r6         ; Hk (must be even; r6 = r0+6, even number)
LoadTestReg 0, r7         ; Hk+1
LoadFlags noflags
butt r4, r5, r6
StoreResult r6            ; check sum
TestFlags noflags         ; positive non-zero result, no flags set
TestResult 13
StoreResult r7            ; check diff
TestResult 7

;BUTT zero sum: 5 + (-5) = 0.  Z=1 from add result.
SetTestNumber 133
LoadTestReg $FFFFFFFB, r4 ; -5
LoadTestReg 5, r5
LoadTestReg $DEADBEEF, r6
LoadTestReg $DEADBEEF, r7
LoadFlags noflags
butt r4, r5, r6
StoreResult r6
TestFlags zf+cf           ; sum=0 ? Z; -5 + 5 = 0 with carry-out ? C
TestResult 0
StoreResult r7
TestResult 10             ; 5 - (-5) = 10

;BUTT signed overflow: $7FFFFFFF + 1 = $80000000.  V=1 (sign overflow), N=1.
SetTestNumber 134
LoadTestReg 1, r4
LoadTestReg $7FFFFFFF, r5
LoadTestReg 0, r6
LoadTestReg 0, r7
LoadFlags noflags
butt r4, r5, r6
StoreResult r6
TestFlags nf+vf
TestResult $80000000
StoreResult r7
TestResult $7FFFFFFE      ; $7FFFFFFF - 1 = $7FFFFFFE (no flag-effect from diff)

;==================================================================
; Some Branch condition codes
; Each test uses cmp/op to set up a known CC state, then a conditional
; jmp.  If the condition fires when expected, control jumps to a pass
; label; if it doesn't fire, fallthrough hits an unconditional jmp to
; error.  The "should not fire" direction is tested implicitly:
; the next test starts with its own CC setup and would mis-fire if the
; opposite condition was decoded incorrectly.
;
; OEM spec lists all 22 conditions and their flag tests.
; lt = (n.~v) + (~n.v)   = N XOR V
; le = z + lt
; gt = ~z . ~lt          (= ~(le))
; ge = ~lt
; cs = c                 (Carry set, "Low")
; cc = ~c                (Carry clear, "High or same")
; hi = ~c . ~z           (High, unsigned >)
; lo = c + z             (Low or same, unsigned <=)
; vs = v
; vc = ~v
; mvs = mv
; mvc = ~mv
; mi = n
; pl = ~n
;==================================================================

`test_bcond_lt_ge:

;CMP 1, 0 ? r5 - r4 = -1 = $FFFFFFFF.  N=1, V=0 ? lt is TRUE.
;Verify: jmp lt fires.
SetTestNumber 135
LoadTestReg 1, r4
LoadTestReg 0, r5
LoadFlags noflags
cmp r4, r5                ; sets N=1, C=1, V=0, Z=0
mv_s #bcond_135_pass, branchTargetReg
jmp lt, (branchTargetReg), nop   ; lt = N XOR V = 1 XOR 0 = 1, should fire
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_135_pass:

;CMP 0, 1 ? r5 - r4 = 1.  N=0, V=0 ? lt is FALSE, ge is TRUE.
;Verify: jmp ge fires (and that ge isn't confused with eq/ne).
SetTestNumber 136
LoadTestReg 0, r4
LoadTestReg 1, r5
LoadFlags noflags
cmp r4, r5                ; sets N=0, C=0, V=0, Z=0
mv_s #bcond_136_pass, branchTargetReg
jmp ge, (branchTargetReg), nop   ; ge = ~lt = 1, should fire
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_136_pass:

`test_bcond_signed_overflow:

;CMP $80000000, $7FFFFFFF ? MAX - MIN signed-overflow.
;Per test 120: N=1, V=1, C=1, Z=0.  N XOR V = 0 ? lt is FALSE, ge is TRUE.
;This is the classic "signed compare differs from arithmetic compare" case
;a buggy emulator might miss.
SetTestNumber 137
LoadTestReg $80000000, r4
LoadTestReg $7FFFFFFF, r5
LoadFlags noflags
cmp r4, r5                ; N=1, V=1, C=1
mv_s #bcond_137_pass, branchTargetReg
jmp ge, (branchTargetReg), nop   ; ge = ~(N XOR V) = ~0 = 1; should fire
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_137_pass:

`test_bcond_le_gt:

;CMP 5, 5 ? equal. Z=1.  le = z + lt = 1 + 0 = 1.  gt = ~le = 0.
SetTestNumber 138
LoadTestReg 5, r4
LoadTestReg 5, r5
LoadFlags noflags
cmp r4, r5                ; Z=1, N=0, V=0, C=0
mv_s #bcond_138_pass, branchTargetReg
jmp le, (branchTargetReg), nop   ; le = z+lt = 1, should fire
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_138_pass:

;CMP 4, 5 ? r5 - r4 = 1.  Z=0, N=0, V=0.  gt = ~z.~lt = 1.~0 = 1.
SetTestNumber 139
LoadTestReg 4, r4
LoadTestReg 5, r5
LoadFlags noflags
cmp r4, r5
mv_s #bcond_139_pass, branchTargetReg
jmp gt, (branchTargetReg), nop
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_139_pass:

`test_bcond_carry:

;CMP 5, 4 ? r5 - r4 = -1 = $FFFFFFFF.  C=1 (borrow).  lo = c+z = 1.
SetTestNumber 140
LoadTestReg 5, r4
LoadTestReg 4, r5
LoadFlags noflags
cmp r4, r5
mv_s #bcond_140_pass, branchTargetReg
jmp lo, (branchTargetReg), nop   ; lo = c+z, C=1 here ? fire
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_140_pass:

;CMP 4, 5 ? C=0, Z=0.  hi = ~c.~z = 1.1 = 1 ? fire.
SetTestNumber 141
LoadTestReg 4, r4
LoadTestReg 5, r5
LoadFlags noflags
cmp r4, r5
mv_s #bcond_141_pass, branchTargetReg
jmp hi, (branchTargetReg), nop
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_141_pass:

`test_bcond_overflow:

;ADD $7FFFFFFF + 1 = $80000000 ? V=1.  vs should fire.
SetTestNumber 142
LoadTestReg $7FFFFFFF, r4
LoadTestReg 1, r5
LoadTestReg 0, r6
LoadFlags noflags
add r4, r5, r6            ; V=1, N=1, C=0, Z=0
mv_s #bcond_142_pass, branchTargetReg
jmp vs, (branchTargetReg), nop
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_142_pass:

`test_bcond_mv:

;MUL with overflow (test 125 pattern) sets mv=1.  mvs should fire.
SetTestNumber 143
LoadTestReg $7FFFFFFF, r4
LoadTestReg $7FFFFFFF, r5
LoadFlags noflags
mul r4, r5, >>#0, r5
nop                       ; 2-cycle latency before flags valid
mv_s #bcond_143_pass, branchTargetReg
jmp mvs, (branchTargetReg), nop
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_143_pass:

;Small mul: 2*3 ? no overflow ? mv=0.  mvc should fire.
SetTestNumber 144
LoadTestReg 2, r4
LoadTestReg 3, r5
LoadFlags noflags
mul r4, r5, >>#0, r5
nop
mv_s #bcond_144_pass, branchTargetReg
jmp mvc, (branchTargetReg), nop
mv_s #error, branchTargetReg
jmp (branchTargetReg), nop
bcond_144_pass:

;==================================================================
; Loads/stores with linear (register-indirect) addressing.
; Uses the test caller's scratch buffer (scratchBufferReg).  At test
; entry the framework has saved v3..v7 there at offsets 0..63;
; offsets 64..127 (48 bytes / 12 scalars) are free for tests to use.
; We work via scratchBufferReg + 16 (offset 80, fully past v3) so we
; can't corrupt the saved-vector area.
;
; Latency rules (via OEM spec):
;   ld_s, ld_v, ld_w all complete in 2 cycles - need a nop between
;   the load and the first read of the destination register.
;   Stores are not latched; no special latency for st_s/st_v.
; Condition codes: all listed loads leave CC unchanged.
;==================================================================

`test_st_ld_scalar:

;Round-trip a scalar through memory: write $12345678 via st_s, read
;back via ld_s, verify register contains the same value.  CC must be
;preserved (load/store don't touch flags).
SetTestNumber 145
LoadTestReg $12345678, r4
LoadTestReg $DEADBEEF, r8        ; sentinel - must be overwritten by ld
mv_s scratchBufferReg, r5
add #16, r5                      ; safely past the saved-v3 area
LoadFlags allflags
st_s r4, (r5)
ld_s (r5), r8
nop                              ; 2-cycle ld latency
StoreResult r8
TestFlags allflags               ; flags untouched by ld_s + st_s
TestResult $12345678

`test_st_ld_vector:

;Vector round-trip: build v1 = ($11111111,$22222222,$33333333,$44444444),
;st_v to scratch, ld_v back into v3 = (r12..r15), verify each lane.
SetTestNumber 146
LoadTestReg $11111111, r4        ; v1[0]
LoadTestReg $22222222, r5        ; v1[1]
LoadTestReg $33333333, r6        ; v1[2]
LoadTestReg $44444444, r7        ; v1[3]
LoadTestReg 0, r12               ; v3 dst lanes - pre-clear so any
LoadTestReg 0, r13               ; un-loaded lane shows up obvious
LoadTestReg 0, r14
LoadTestReg 0, r15
mv_s scratchBufferReg, r8
add #16, r8                      ; r8 = scratch + 16 (past v3 save)
LoadFlags allflags
st_v v1, (r8)
ld_v (r8), v3
nop                              ; 2-cycle ld latency
StoreResult r12
TestFlags allflags
TestResult $11111111
StoreResult r13
TestResult $22222222
StoreResult r14
TestResult $33333333
StoreResult r15
TestResult $44444444

`test_ld_word:

;ld_w loads a 16-bit value into bits 31-16 of dest, zeroing 15-0
;Memory is big-endian, so the high 16 bits of a
;st_s'd 32-bit word are at the lower address - exactly what ld_w reads.
;Store $A1B2C3D4, then ld_w ? register should be $A1B20000.
SetTestNumber 147
LoadTestReg $A1B2C3D4, r4
LoadTestReg $DEADBEEF, r8
mv_s scratchBufferReg, r5
add #16, r5
LoadFlags allflags
st_s r4, (r5)
ld_w (r5), r8
nop
StoreResult r8
TestFlags allflags               ; ld_w preserves flags
TestResult $A1B20000

allpass:

;Return $0 to indicate success
SetStatus $0
mv_s #success, branchTargetReg
jmp (branchTargetReg), nop
SetStatus $DEADF00D
SetStatus $DEADF00D

error:
;Return test number that failed
mv_s testNumberReg, testStatusReg

success:
mv_s #doquit, branchTargetReg
jmp (branchTargetReg), nop
SetStatus $DEADF00D
SetStatus $DEADF00D

doquit:

st_s returnAddressReg, rz
nop
;save test results
mv_s expectedFlagsReg, r4
mv_s resultFlagsReg, r5
mv_s expectedResultReg, r6
mv_s resultValueReg, r7
{
ld_v (scratchBufferReg), v3
sub #16, scratchBufferReg
}
{
ld_v (scratchBufferReg), v4
sub #16, scratchBufferReg
}
{
ld_v (scratchBufferReg), v5
sub #16, scratchBufferReg
}
{
ld_v (scratchBufferReg), v6
sub #16, scratchBufferReg
}
ld_v (scratchBufferReg), v7
{
st_s r4, (scratchBufferReg)
add #4, scratchBufferReg
}
{
st_s r5, (scratchBufferReg)
add #4, scratchBufferReg
}
{
st_s r6, (scratchBufferReg)
add #4, scratchBufferReg
}
{
st_s r7, (scratchBufferReg)
rts nop
}


