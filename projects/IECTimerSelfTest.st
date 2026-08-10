(******************************************************************************
 IEC timer function-block capture test for GX Works2 / FX2N(C)

 Add these POU-local labels with Class VAR before compiling:
   TON_1 : TON_E
   TOF_1 : TOF_E
   TP_1  : TP_E

 Run this POU by itself so no other program writes Y0-Y2.
******************************************************************************)

TON_1(IN := X0, PT := T#2s);
Y0 := TON_1.Q;

TOF_1(IN := X1, PT := T#2s);
Y1 := TOF_1.Q;

TP_1(IN := X2, PT := T#2s);
Y2 := TP_1.Q;
