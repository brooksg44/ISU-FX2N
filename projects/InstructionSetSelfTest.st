(******************************************************************************
 ISU-FX2N instruction-set self-test for GX Works2 Structured Project / FX2N(C)

 Run once on M8002 (initial RUN pulse).  Monitor D0-D71, M0-M207, and Y0-Y6.
 Y0-Y5 are group-pass indicators.  Y6 is the overall PASS indicator.
 If Y6 is OFF, inspect M200-M205 to find the failing group.

 Add these POU-local labels before compiling:
   TON_1 : TON_E
   TOF_1 : TOF_E
   TP_1  : TP_E
   TON_N : TON
   TOF_N : TOF
   TP_N  : TP
******************************************************************************)

(* Move and compare --------------------------------------------------------- *)
MOV(M8002, K5, D0);
MOV(M8002, K3, D1);
CMP(M8002, D0, D1, M0);             (* M0=1, M1=0, M2=0 *)

MOV(M8002, H1111, D10);
MOV(M8002, H2222, D11);
BMOV(M8002, D10, K2, D12);          (* D12=1111h, D13=2222h *)
FMOV(M8002, H55AA, K3, D14);        (* D14-D16=55AAh *)

(* Arithmetic and word logic ----------------------------------------------- *)
ADD_E(M8002, K7, K3, D20);          (* 10 *)
SUB_E(M8002, K7, K10, D21);         (* -3 / FFFDh *)
MUL_E(M8002, K-2, K3, D22);         (* D23:D22 = -6 *)
DIV_E(M8002, K7, K3, D24);          (* quotient D24=2; DIV_E has no remainder output *)
WAND(M8002, H0FF0, H00FF, D26);     (* 00F0h *)
WOR(M8002, H00F0, H000F, D27);      (* 00FFh *)
WXOR(M8002, H00AA, H00FF, D28);     (* 0055h *)
MOV(M8002, K9, D29);
INC(M8002, D29);                    (* 10 *)
DEC(M8002, D29);                    (* back to 9 *)
MOV(M8002, K5, D30);
NEG(M8002, D30);                    (* -5 / FFFBh *)

(* Rotate and bit shifts ---------------------------------------------------- *)
MOV(M8002, H0001, D40);
ROR(M8002, K1, D40);                (* 8000h *)
MOV(M8002, H1001, D41);
ROL(M8002, K4, D41);                (* 0011h *)

(* Hold X0 ON before entering RUN: SFTR loads it into M103. *)
SFTR(M8002, X0, K4, K1, M100);
(* Hold X1 ON before entering RUN: SFTL loads it into M110. *)
SFTL(M8002, X1, K4, K1, M110);

(* FIFO: D50 is pointer; D51 onward holds data. *)
MOV(M8002, K0, D50);
SFWR(M8002, K111, K4, D50);
SFWR(M8002, K222, K4, D50);
SFRD(M8002, D50, K4, D55);          (* D55=111, D50=1, D51=222 *)

(* Encode and float conversion --------------------------------------------- *)
MOV(M8002, H0008, D61);
ENCO(M8002, D61, K4, D60);          (* bit 3 is highest ON bit: D60=3 *)
MOV(M8002, K-3, D69);
FLT(M8002, D69, D70);               (* D71:D70 = C0400000h (-3.0) *)

(* Inline comparison contacts.  These exercise LD=, >, <, <>, <= and >=. *)
M180 := D0 = K5;
M181 := D0 > D1;
M182 := D1 < D0;
M183 := D0 <> D1;
M184 := D1 <= D0;
M185 := D0 >= D1;

(* Stable group results ----------------------------------------------------- *)
M200 := M0 AND (NOT M1) AND (NOT M2) AND
        (D12 = H1111) AND (D13 = H2222) AND
        (D14 = H55AA) AND (D15 = H55AA) AND (D16 = H55AA);

M201 := (D20 = K10) AND (D21 = K-3) AND
        (D22 = HFFFA) AND (D23 = HFFFF) AND
        (D24 = K2) AND (D25 = K0) AND
        (D26 = H00F0) AND (D27 = H00FF) AND
        (D28 = H0055) AND (D29 = K9) AND (D30 = K-5);

M202 := (D40 = H8000) AND (D41 = H0011);
M203 := (D55 = K111) AND (D50 = K1) AND (D51 = K222);
M204 := (D60 = K3) AND (D70 = H0000) AND (D71 = HC040);
M205 := M180 AND M181 AND M182 AND M183 AND M184 AND M185;

(* IEC timers -------------------------------------------------------------- *)
(* X2 must remain ON for 2 s before M190 turns ON. *)
TON_1(EN := M8000, IN := X2, PT := T#2s);
M190 := TON_1.Q;

(* M191 turns ON with X3 and remains ON for 2 s after X3 turns OFF. *)
TOF_1(EN := M8000, IN := X3, PT := T#2s);
M191 := TOF_1.Q;

(* A rising edge on X4 turns M192 ON for 2 s. *)
TP_1(EN := M8000, IN := X4, PT := T#2s);
M192 := TP_1.Q;

(* Plain IEC forms have no EN/ENO parameters. *)
(* X5 must remain ON for 2 s before M193 turns ON. *)
TON_N(IN := X5, PT := T#2s);
M193 := TON_N.Q;

(* M194 turns ON with X6 and remains ON for 2 s after X6 turns OFF. *)
TOF_N(IN := X6, PT := T#2s);
M194 := TOF_N.Q;

(* A rising edge on X7 turns M195 ON for 2 s. *)
TP_N(IN := X7, PT := T#2s);
M195 := TP_N.Q;

Y0 := M200;
Y1 := M201;
Y2 := M202;
Y3 := M203;
Y4 := M204;
Y5 := M205;
Y6 := M200 AND M201 AND M202 AND M203 AND M204 AND M205;
