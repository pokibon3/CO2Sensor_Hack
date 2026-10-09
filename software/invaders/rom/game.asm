; ALIEN RAID - original invaders-style game for Space-Invaders-compatible hardware
; DM72D version: bold font, Nagoya attack (aliens may reach the row above the player)
; (8080, ROM 0000-1FFF, RAM 2000-23FF, VRAM 2400-3FFF, shifter on ports 2/3/4)
;
; Video RAM: column-major, rotated. Address = 2400h + col*32 + (x >> 3), bit = x & 7,
;   col = display column 0..223 (left -> right), x = display row from the BOTTOM 0..255.
; Sprites are column bytes, bit 7 = top.

VRAM     EQU 2400H

; IN 1 bits
IN_START EQU 04H
IN_FIRE  EQU 10H
IN_LEFT  EQU 20H
IN_RIGHT EQU 40H

; ---------------- RAM ----------------
FRAMEFLG EQU 2000H
FRAMECNT EQU 2001H
RNGL     EQU 2002H          ; 2 bytes
STATE    EQU 2004H          ; 0 attract, 1 play, 2 dying, 3 game over
SCORE    EQU 2005H          ; 2 bytes BCD (lo, hi)
HISCORE  EQU 2007H          ; 2 bytes BCD
LIVES    EQU 2009H
WAVE     EQU 200AH
PLX      EQU 200BH
PB_ACT   EQU 200CH
PB_COL   EQU 200DH
PB_X     EQU 200EH
BOMBS    EQU 2010H          ; 3 x (act, col, x, -)
BOMBTMR  EQU 201CH
AL_IDX   EQU 201DH
AL_DIR   EQU 201EH          ; 0 right, 1 left
AL_MODE  EQU 201FH          ; 0 sideways, 1 down
AL_EDGE  EQU 2020H
AL_ANIM  EQU 2021H
AL_CNT   EQU 2022H
UFO_ACT  EQU 2023H
UFO_COL  EQU 2024H
UFO_DIR  EQU 2025H
UFO_TMR  EQU 2026H          ; 2 bytes
EXP_TMR  EQU 2028H
EXP_COL  EQU 2029H
EXP_X    EQU 202AH
DEATH_TMR EQU 202BH
UFO_STMR EQU 202CH
UFO_SCOL EQU 202DH
SCOREDIRTY EQU 202EH
INVADED  EQU 202FH
CHIPPAT  EQU 2030H
CHIPCOL  EQU 2031H
CHIPX    EQU 2032H
CHIPN    EQU 2033H
INHELD   EQU 2034H
INNEW    EQU 2035H
TMP      EQU 2036H
TMP2     EQU 2037H
BPTR     EQU 2038H          ; 2 bytes
BCNT     EQU 203AH
BPTR2    EQU 203BH          ; 2 bytes
CPTR     EQU 203DH          ; 2 bytes
CCNT     EQU 203FH
ALIENS   EQU 2040H          ; 55 x (alive, col, x, type)
STACKTOP EQU 2400H

PLAYER_X EQU 32             ; player sprite row (byte aligned)
UFO_X    EQU 208
SHIELD_X EQU 48

; ---------------- vectors ----------------
        ORG  0
        JMP  START
        ORG  8              ; RST 1 : mid frame
        EI
        RET
        ORG  10H            ; RST 2 : end of frame
        JMP  ISR_FRAME

        ORG  20H
ISR_FRAME:
        PUSH PSW
        LDA  FRAMECNT
        INR  A
        STA  FRAMECNT
        MVI  A, 1
        STA  FRAMEFLG
        POP  PSW
        EI
        RET

START:
        LXI  SP, STACKTOP
        LXI  H, 2000H
CLRRAM: MVI  M, 0
        INX  H
        MOV  A, H
        CPI  24H
        JNZ  CLRRAM
        LXI  H, 0ACE1H
        SHLD RNGL
        EI
        JMP  ATTRACT

; =====================================================================
; Attract / title
; =====================================================================
ATTRACT:
        XRA  A
        STA  STATE
        CALL CLS
        CALL DRAWSCORES
        CALL DRAWLINE
        LXI  H, STR_TITLE
        LXI  D, 72*256+200
        CALL DRAWSTR
        LXI  H, STR_PUSH
        LXI  D, 72*256+160
        CALL DRAWSTR
        LXI  H, SPR_UFO
        LXI  D, 40*256+128
        MVI  B, 20
        CALL DRAWSPR
        LXI  H, STR_PMYS
        LXI  D, 64*256+128
        CALL DRAWSTR
        LXI  H, SPR_A0
        LXI  D, 44*256+112
        MVI  B, 16
        CALL DRAWSPR
        LXI  H, STR_P30
        LXI  D, 64*256+112
        CALL DRAWSTR
        LXI  H, SPR_B0
        LXI  D, 44*256+96
        MVI  B, 16
        CALL DRAWSPR
        LXI  H, STR_P20
        LXI  D, 64*256+96
        CALL DRAWSTR
        LXI  H, SPR_C0
        LXI  D, 44*256+80
        MVI  B, 16
        CALL DRAWSPR
        LXI  H, STR_P10
        LXI  D, 64*256+80
        CALL DRAWSTR
ATLOOP: CALL WAITF
        CALL RAND
        CALL READIN
        LDA  INNEW
        ANI  IN_START+IN_FIRE
        JZ   ATLOOP

NEWGAME:
        LXI  H, 0
        SHLD SCORE
        MVI  A, 3
        STA  LIVES
        XRA  A
        STA  WAVE
        CALL CLS
        CALL DRAWSCORES
        CALL DRAWLINE
        CALL DRAWLIVES
        CALL INITWAVE

; =====================================================================
; Main game loop (one pass per frame)
; =====================================================================
GL:     CALL WAITF
        CALL READIN
        LDA  STATE
        CPI  3
        JZ   GAMEOVER
        CPI  2
        JZ   GL_DYING
        CALL PLAYERUPD
        CALL FIREUPD
        CALL PBUPD
        CALL ALSTEP
        CALL ALSTEP
        CALL BOMBUPD
        CALL UFOUPD
        CALL EXPUPD
        LDA  INVADED
        ORA  A
        JZ   GL_1
        MVI  A, 3
        STA  STATE
GL_1:   LDA  AL_CNT
        ORA  A
        JNZ  GL_END
        ; wave cleared
        CALL CLEARSHOTS
        CALL SCOREUPD
        MVI  B, 60
        CALL WAITN
        LDA  WAVE
        INR  A
        STA  WAVE
        CALL INITWAVE
        JMP  GL
GL_DYING:
        CALL DEATHUPD
        CALL EXPUPD
        CALL UFOUPD
GL_END: CALL SCOREUPD
        JMP  GL

GAMEOVER:
        LXI  H, STR_OVER
        LXI  D, 76*256+192
        CALL DRAWSTR
        CALL SCOREVAL
        MVI  B, 180
        CALL WAITN
        JMP  ATTRACT

; =====================================================================
; Wave setup
; =====================================================================
INITWAVE:
        CALL CLRFIELD
        MVI  D, 30
        CALL DRAWSHIELD
        MVI  D, 78
        CALL DRAWSHIELD
        MVI  D, 126
        CALL DRAWSHIELD
        MVI  D, 174
        CALL DRAWSHIELD
        ; top row x = 184 - 8 * min(wave, 4)
        LDA  WAVE
        CPI  4
        JC   IW_1
        MVI  A, 4
IW_1:   ADD  A
        ADD  A
        ADD  A
        MOV  B, A
        MVI  A, 184
        SUB  B
        STA  TMP
        LXI  H, ALIENS
        MVI  C, 0               ; row
IW_ROW: MVI  B, 0               ; column index
        MVI  D, 24              ; screen column
        MOV  A, C
        ADD  A
        ADD  A
        ADD  A
        ADD  A
        MOV  E, A
        LDA  TMP
        SUB  E
        MOV  E, A               ; x of this row
        MOV  A, C
        ORA  A
        MVI  A, 0
        JZ   IW_T
        MOV  A, C
        CPI  3
        MVI  A, 1
        JC   IW_T
        MVI  A, 2
IW_T:   STA  TMP2
IW_COL: MVI  M, 1
        INX  H
        MOV  M, D
        INX  H
        MOV  M, E
        INX  H
        LDA  TMP2
        MOV  M, A
        INX  H
        MOV  A, D
        ADI  16
        MOV  D, A
        INR  B
        MOV  A, B
        CPI  11
        JNZ  IW_COL
        INR  C
        MOV  A, C
        CPI  5
        JNZ  IW_ROW
        XRA  A
        STA  AL_IDX
        STA  AL_DIR
        STA  AL_MODE
        STA  AL_EDGE
        STA  AL_ANIM
        STA  INVADED
        STA  PB_ACT
        STA  UFO_ACT
        STA  UFO_STMR
        STA  EXP_TMR
        STA  BOMBS
        STA  BOMBS+4
        STA  BOMBS+8
        MVI  A, 55
        STA  AL_CNT
        MVI  A, 60
        STA  BOMBTMR
        LXI  H, 600
        SHLD UFO_TMR
        LXI  H, ALIENS
        MVI  C, 55
IW_DRAW:
        PUSH H
        PUSH B
        CALL DRAWALIEN
        POP  B
        POP  H
        INX  H
        INX  H
        INX  H
        INX  H
        DCR  C
        JNZ  IW_DRAW
        MVI  A, 104
        STA  PLX
        MVI  A, 1
        STA  STATE
        RET

; D = column
DRAWSHIELD:
        MVI  E, SHIELD_X
        CALL CALCADDR
        LXI  D, SPR_SHIELD
        MVI  B, 22
DSH_1:  LDAX D
        MOV  M, A
        INX  D
        INX  H
        LDAX D
        MOV  M, A
        INX  D
        DCX  H
        MOV  A, L
        ADI  32
        MOV  L, A
        JNC  DSH_2
        INR  H
DSH_2:  DCR  B
        JNZ  DSH_1
        RET

; =====================================================================
; Aliens
; =====================================================================
; A = index -> HL = entry
ALIENPTR:
        MOV  L, A
        MVI  H, 0
        DAD  H
        DAD  H
        PUSH D
        LXI  D, ALIENS
        DAD  D
        POP  D
        RET

; HL = entry -> draw at its position with current animation frame
DRAWALIEN:
        INX  H
        MOV  D, M
        INX  H
        MOV  E, M
        INX  H
        MOV  A, M               ; type
        ADD  A
        MOV  C, A
        LDA  AL_ANIM
        ADD  C
        ADD  A
        MOV  C, A
        MVI  B, 0
        LXI  H, SPRTAB
        DAD  B
        MOV  A, M
        INX  H
        MOV  H, M
        MOV  L, A
        MVI  B, 16
        JMP  DRAWSPR

; move the next living alien one step
ALSTEP:
        LDA  AL_CNT
        ORA  A
        RZ
AS_FIND:
        LDA  AL_IDX
        CPI  55
        JC   AS_1
        CALL ENDPASS
        XRA  A
        STA  AL_IDX
AS_1:   CALL ALIENPTR
        MOV  A, M
        ORA  A
        JNZ  AS_FOUND
        LDA  AL_IDX
        INR  A
        STA  AL_IDX
        JMP  AS_FIND
AS_FOUND:
        PUSH H
        LDA  AL_MODE
        ORA  A
        JZ   AS_SIDE
        ; step down: erase old position, x -= 8
        INX  H
        MOV  D, M
        INX  H
        MOV  E, M
        MVI  B, 16
        PUSH H
        CALL CLRSPR
        POP  H
        MOV  A, M
        SUI  8
        MOV  M, A
        JMP  AS_DRAW
AS_SIDE:
        INX  H
        LDA  AL_DIR
        ORA  A
        MVI  A, 2
        JZ   AS_2
        MVI  A, 0FEH
AS_2:   ADD  M
        MOV  M, A
        CPI  206
        JNC  AS_EDGE
        CPI  3
        JNC  AS_DRAW
AS_EDGE:
        MVI  A, 1
        STA  AL_EDGE
AS_DRAW:
        POP  H
        PUSH H
        CALL DRAWALIEN
        POP  H
        INX  H
        INX  H
        MOV  A, M
        CPI  PLAYER_X+1         ; invaded only when an alien lands on the player row
        JNC  AS_3
        MVI  A, 1
        STA  INVADED
AS_3:   LDA  AL_IDX
        INR  A
        STA  AL_IDX
        RET

ENDPASS:
        LDA  AL_ANIM
        XRI  1
        STA  AL_ANIM
        LDA  AL_MODE
        ORA  A
        JZ   EP_1
        XRA  A
        STA  AL_MODE
        RET
EP_1:   LDA  AL_EDGE
        ORA  A
        RZ
        XRA  A
        STA  AL_EDGE
        MVI  A, 1
        STA  AL_MODE
        LDA  AL_DIR
        XRI  1
        STA  AL_DIR
        RET

; HL = entry of a hit alien
ALKILL:
        MVI  M, 0
        LDA  AL_CNT
        DCR  A
        STA  AL_CNT
        PUSH H
        CALL EXPCLR
        POP  H
        INX  H
        MOV  D, M
        INX  H
        MOV  E, M
        INX  H
        MOV  A, M               ; type 0/1/2 -> 30/20/10 points
        PUSH D
        ADD  A
        ADD  A
        ADD  A
        ADD  A
        MOV  C, A
        MVI  A, 30H
        SUB  C
        CALL ADDSCORE
        POP  D
        MOV  A, D
        STA  EXP_COL
        MOV  A, E
        STA  EXP_X
        MVI  A, 12
        STA  EXP_TMR
        LXI  H, SPR_EXPL
        MVI  B, 16
        JMP  DRAWSPR

EXPCLR: LDA  EXP_TMR
        ORA  A
        RZ
        XRA  A
        STA  EXP_TMR
EXPCLR0:
        LDA  EXP_COL
        MOV  D, A
        LDA  EXP_X
        MOV  E, A
        MVI  B, 16
        JMP  CLRSPR

EXPUPD: LDA  EXP_TMR
        ORA  A
        RZ
        DCR  A
        STA  EXP_TMR
        RNZ
        JMP  EXPCLR0

; =====================================================================
; Player
; =====================================================================
PLAYERUPD:
        LDA  INHELD
        MOV  B, A
        LDA  PLX
        MOV  C, A
        MOV  A, B
        ANI  IN_LEFT
        JZ   PU_1
        MOV  A, C
        CPI  2
        JC   PU_1
        DCR  C
PU_1:   MOV  A, B
        ANI  IN_RIGHT
        JZ   PU_2
        MOV  A, C
        CPI  206
        JNC  PU_2
        INR  C
PU_2:   MOV  A, C
        STA  PLX
        MOV  D, A
        MVI  E, PLAYER_X
        LXI  H, SPR_PLAYER
        MVI  B, 16
        JMP  DRAWSPR

FIREUPD:
        LDA  INNEW
        ANI  IN_FIRE
        RZ
        LDA  PB_ACT
        ORA  A
        RNZ
        MVI  A, 1
        STA  PB_ACT
        LDA  PLX
        ADI  7
        STA  PB_COL
        MOV  B, A
        MVI  A, PLAYER_X+8
        STA  PB_X
        MOV  C, A
        MVI  A, 0FH
        CALL PREPBITS
        JMP  ORBITS

PBUPD:  LDA  PB_ACT
        ORA  A
        RZ
        LDA  PB_COL
        MOV  B, A
        LDA  PB_X
        MOV  C, A
        MVI  A, 0FH
        CALL PREPBITS
        CALL CLRBITS
        LDA  PB_X
        ADI  4
        STA  PB_X
        CPI  220
        JNC  PB_END
        MOV  C, A
        LDA  PB_COL
        MOV  B, A
        MVI  A, 0FH
        CALL PREPBITS
        CALL TESTBITS
        JNZ  PB_HIT
        JMP  ORBITS
PB_HIT: XRA  A
        STA  PB_ACT
        JMP  PBHIT
PB_END: XRA  A
        STA  PB_ACT
        RET

; player bullet hit something at (PB_COL, PB_X)
PBHIT:
        LDA  UFO_ACT
        ORA  A
        JZ   PH_BOMB
        LDA  PB_X
        CPI  UFO_X-4
        JC   PH_BOMB
        LDA  UFO_COL
        MOV  B, A
        LDA  PB_COL
        SUB  B
        SUI  2
        CPI  16
        JNC  PH_BOMB
        JMP  UFOKILL
PH_BOMB:
        LXI  H, BOMBS
        MVI  C, 3
PH_B1:  MOV  A, M
        ORA  A
        JZ   PH_B2
        INX  H
        LDA  PB_COL
        CMP  M
        DCX  H
        JNZ  PH_B2
        INX  H
        INX  H
        LDA  PB_X
        ADI  4
        SUB  M
        DCX  H
        DCX  H
        CPI  12
        JNC  PH_B2
        JMP  KILLBOMB
PH_B2:  INX  H
        INX  H
        INX  H
        INX  H
        DCR  C
        JNZ  PH_B1
        ; aliens
        LXI  H, ALIENS
        MVI  C, 0
PH_A1:  MOV  A, M
        ORA  A
        JZ   PH_A4
        INX  H
        LDA  PB_COL
        SUB  M
        SUI  2
        CPI  12
        JNC  PH_A3
        INX  H
        LDA  PB_X
        ADI  3
        SUB  M
        CPI  11
        JNC  PH_A2
        DCX  H
        DCX  H
        JMP  ALKILL
PH_A2:  DCX  H
PH_A3:  DCX  H
PH_A4:  INX  H
        INX  H
        INX  H
        INX  H
        INR  C
        MOV  A, C
        CPI  55
        JNZ  PH_A1
        ; shield?
        LDA  PB_X
        CPI  SHIELD_X-4
        RC
        CPI  SHIELD_X+18
        RNC
        LDA  PB_COL
        MOV  B, A
        LDA  PB_X
        MOV  C, A
        MVI  A, 0FH
        JMP  CHIP

PLAYERDIE:
        MVI  A, 2
        STA  STATE
        MVI  A, 100
        STA  DEATH_TMR
        JMP  CLEARSHOTS

DEATHUPD:
        LDA  DEATH_TMR
        DCR  A
        STA  DEATH_TMR
        JZ   DU_DONE
        ANI  08H
        LXI  H, SPR_PX0
        JZ   DU_1
        LXI  H, SPR_PX1
DU_1:   LDA  PLX
        MOV  D, A
        MVI  E, PLAYER_X
        MVI  B, 16
        JMP  DRAWSPR
DU_DONE:
        LDA  PLX
        MOV  D, A
        MVI  E, PLAYER_X
        MVI  B, 16
        CALL CLRSPR
        LDA  LIVES
        DCR  A
        STA  LIVES
        CALL DRAWLIVES
        LDA  LIVES
        ORA  A
        MVI  A, 3
        JZ   DU_2
        MVI  A, 1
DU_2:   STA  STATE
        RET

; erase player bullet and all bombs
CLEARSHOTS:
        LDA  PB_ACT
        ORA  A
        JZ   CS_1
        XRA  A
        STA  PB_ACT
        LDA  PB_COL
        MOV  B, A
        LDA  PB_X
        MOV  C, A
        MVI  A, 0FH
        CALL PREPBITS
        CALL CLRBITS
CS_1:   LXI  H, BOMBS
        SHLD CPTR
        MVI  A, 3
        STA  CCNT
CS_2:   LHLD CPTR
        MOV  A, M
        ORA  A
        CNZ  KILLBOMB
        LHLD CPTR
        INX  H
        INX  H
        INX  H
        INX  H
        SHLD CPTR
        LDA  CCNT
        DCR  A
        STA  CCNT
        JNZ  CS_2
        RET

; HL = bomb slot: deactivate and erase
KILLBOMB:
        MVI  M, 0
        INX  H
        MOV  B, M
        INX  H
        MOV  C, M
        MVI  A, 0FH
        CALL PREPBITS
        JMP  CLRBITS

; =====================================================================
; Alien bombs
; =====================================================================
BOMBUPD:
        LDA  BOMBTMR
        DCR  A
        STA  BOMBTMR
        JNZ  BU_MOVE
        LDA  WAVE
        CPI  7
        JC   BU_1
        MVI  A, 7
BU_1:   ADD  A
        ADD  A
        MOV  B, A
        MVI  A, 40
        SUB  B
        STA  BOMBTMR
        CALL SPAWNBOMB
BU_MOVE:
        LXI  H, BOMBS
        SHLD BPTR
        MVI  A, 3
        STA  BCNT
BU_L:   LHLD BPTR
        MOV  A, M
        ORA  A
        JZ   BU_NEXT
        INX  H
        MOV  B, M
        INX  H
        MOV  C, M
        MVI  A, 0FH
        CALL PREPBITS
        CALL CLRBITS
        LHLD BPTR
        INX  H
        INX  H
        MOV  A, M
        SUI  2
        MOV  M, A
        CPI  18
        JC   BU_GONE
        MOV  C, A
        DCX  H
        MOV  B, M
        MVI  A, 0FH
        CALL PREPBITS
        CALL TESTBITS
        JNZ  BU_HIT
        CALL ORBITS
        JMP  BU_NEXT
BU_GONE:
        LHLD BPTR
        MVI  M, 0
        JMP  BU_NEXT
BU_HIT: CALL BOMBHIT
BU_NEXT:
        LHLD BPTR
        INX  H
        INX  H
        INX  H
        INX  H
        SHLD BPTR
        LDA  BCNT
        DCR  A
        STA  BCNT
        JNZ  BU_L
        RET

; bomb at BPTR hit something (not drawn at its new position)
BOMBHIT:
        LHLD BPTR
        MVI  M, 0
        INX  H
        MOV  B, M
        INX  H
        MOV  C, M
        MOV  A, C
        CPI  PLAYER_X+10
        JNC  BH_2
        LDA  PLX
        MOV  D, A
        MOV  A, B
        SUB  D
        DCR  A
        CPI  14
        RNC
        JMP  PLAYERDIE
BH_2:   LDA  PB_ACT
        ORA  A
        JZ   BH_3
        LDA  PB_COL
        CMP  B
        JNZ  BH_3
        LDA  PB_X
        ADI  4
        SUB  C
        CPI  12
        JNC  BH_3
        XRA  A
        STA  PB_ACT
        LDA  PB_COL
        MOV  B, A
        LDA  PB_X
        MOV  C, A
        MVI  A, 0FH
        CALL PREPBITS
        JMP  CLRBITS
BH_3:   MOV  A, C
        CPI  SHIELD_X-6
        RC
        CPI  SHIELD_X+18
        RNC
        SUI  2
        MOV  C, A
        MVI  A, 1FH
        JMP  CHIP

SPAWNBOMB:
        LXI  H, BOMBS
        MVI  B, 3
SB_1:   MOV  A, M
        ORA  A
        JZ   SB_SLOT
        INX  H
        INX  H
        INX  H
        INX  H
        DCR  B
        JNZ  SB_1
        RET
SB_SLOT:
        SHLD BPTR2
        CALL RAND
        ANI  0FH
        CPI  11
        JC   SB_2
        SUI  8
SB_2:   STA  TMP                ; column index
        MVI  B, 4               ; start from the bottom row
SB_3:   MOV  A, B
        ADD  A
        ADD  A
        ADD  A
        ADD  B
        ADD  B
        ADD  B                  ; 11 * row
        MOV  C, A
        LDA  TMP
        ADD  C
        CALL ALIENPTR
        MOV  A, M
        ORA  A
        JNZ  SB_FOUND
        DCR  B
        JP   SB_3
        RET
SB_FOUND:
        INX  H
        MOV  A, M
        ADI  7
        MOV  D, A
        INX  H
        MOV  A, M
        SUI  6
        ; Nagoya attack: bombs from the row just above the player would start
        ; inside the player and are not released (as on the arcade original)
        CPI  PLAYER_X+8
        RC
        MOV  E, A
        LHLD BPTR2
        MVI  M, 1
        INX  H
        MOV  M, D
        INX  H
        MOV  M, E
        RET

; B = col (center), C = x, A = pattern : clear pattern in 3 columns (shield damage)
CHIP:   STA  CHIPPAT
        MOV  A, B
        DCR  A
        STA  CHIPCOL
        MOV  A, C
        STA  CHIPX
        MVI  A, 3
        STA  CHIPN
CH_1:   LDA  CHIPCOL
        MOV  B, A
        LDA  CHIPX
        MOV  C, A
        LDA  CHIPPAT
        CALL PREPBITS
        CALL CLRBITS
        LDA  CHIPCOL
        INR  A
        STA  CHIPCOL
        LDA  CHIPN
        DCR  A
        STA  CHIPN
        JNZ  CH_1
        RET

; =====================================================================
; UFO
; =====================================================================
UFOUPD:
        LDA  UFO_STMR
        ORA  A
        JZ   UU_1
        DCR  A
        STA  UFO_STMR
        JNZ  UU_1
        LDA  UFO_SCOL
        MOV  D, A
        MVI  E, UFO_X
        MVI  B, 24
        CALL CLRSPR
UU_1:   LDA  UFO_ACT
        ORA  A
        JNZ  UU_MOVE
        LHLD UFO_TMR
        DCX  H
        SHLD UFO_TMR
        MOV  A, H
        ORA  L
        RNZ
        LXI  H, 1200
        SHLD UFO_TMR
        LDA  AL_CNT
        CPI  8
        RC
        LDA  UFO_STMR
        ORA  A
        RNZ
        CALL RAND
        ANI  1
        JZ   UU_LEFT
        MVI  A, 1
        STA  UFO_DIR
        XRA  A
        STA  UFO_COL
        JMP  UU_GO
UU_LEFT:
        MVI  A, 0FFH
        STA  UFO_DIR
        MVI  A, 204
        STA  UFO_COL
UU_GO:  MVI  A, 1
        STA  UFO_ACT
        RET
UU_MOVE:
        LDA  UFO_DIR
        MOV  B, A
        LDA  UFO_COL
        ADD  B
        STA  UFO_COL
        CPI  205
        JNC  UU_END
        MOV  D, A
        MVI  E, UFO_X
        LXI  H, SPR_UFO
        MVI  B, 20
        JMP  DRAWSPR
UU_END: XRA  A
        STA  UFO_ACT
        LDA  UFO_COL
        SUB  B
        MOV  D, A
        MVI  E, UFO_X
        MVI  B, 20
        JMP  CLRSPR

UFOKILL:
        XRA  A
        STA  UFO_ACT
        LDA  UFO_COL
        MOV  D, A
        MVI  E, UFO_X
        MVI  B, 20
        CALL CLRSPR
        CALL RAND
        ANI  3
        STA  TMP
        ADD  A
        MOV  C, A
        MVI  B, 0
        LXI  H, UFOPTS
        DAD  B
        MOV  A, M
        INX  H
        MOV  B, M
        PUSH B
        CALL ADDSCORE
        POP  B
        MOV  A, B
        CALL ADDSCOREHI
        LDA  TMP
        ADD  A
        MOV  C, A
        MVI  B, 0
        LXI  H, UFOSTR
        DAD  B
        MOV  A, M
        INX  H
        MOV  H, M
        MOV  L, A
        LDA  UFO_COL
        ADI  2
        CPI  197
        JC   UK_1
        MVI  A, 196
UK_1:   STA  UFO_SCOL
        MOV  D, A
        MVI  E, UFO_X
        CALL DRAWSTR
        MVI  A, 60
        STA  UFO_STMR
        RET

; =====================================================================
; Score / text
; =====================================================================
; A = BCD points (0-99)
ADDSCORE:
        LXI  H, SCORE
        ADD  M
        DAA
        MOV  M, A
        INX  H
        MOV  A, M
        ACI  0
        DAA
        MOV  M, A
        MVI  A, 1
        STA  SCOREDIRTY
        RET

; A = BCD hundreds
ADDSCOREHI:
        LXI  H, SCORE+1
        ADD  M
        DAA
        MOV  M, A
        MVI  A, 1
        STA  SCOREDIRTY
        RET

SCOREUPD:
        LDA  SCOREDIRTY
        ORA  A
        RZ
        XRA  A
        STA  SCOREDIRTY
        JMP  SCOREVAL

DRAWSCORES:
        LXI  H, STR_SCORE
        LXI  D, 16*256+248
        CALL DRAWSTR
        LXI  H, STR_HI
        LXI  D, 144*256+248
        CALL DRAWSTR
SCOREVAL:
        LHLD SCORE
        XCHG
        LHLD HISCORE
        MOV  A, L
        SUB  E
        MOV  A, H
        SBB  D
        JNC  SV_1
        XCHG
        SHLD HISCORE
SV_1:   LDA  SCORE+1
        LXI  D, 16*256+232
        CALL DRAWBCD
        LDA  SCORE
        CALL DRAWBCD
        LDA  HISCORE+1
        LXI  D, 152*256+232
        CALL DRAWBCD
        LDA  HISCORE
        JMP  DRAWBCD

DRAWLIVES:
        LXI  D, 8*256+0
        MVI  B, 128
        CALL CLRSPR
        LDA  LIVES
        ADI  '0'
        LXI  D, 8*256+0
        CALL DRAWCH
        LDA  LIVES
        CPI  2
        RC
        DCR  A
        MOV  C, A
        MVI  D, 24
DL_1:   PUSH B
        PUSH D
        MVI  E, 0
        LXI  H, SPR_PLAYER
        MVI  B, 16
        CALL DRAWSPR
        POP  D
        POP  B
        MOV  A, D
        ADI  16
        MOV  D, A
        DCR  C
        JNZ  DL_1
        RET

; ground line at x = 15
DRAWLINE:
        LXI  H, VRAM+1
        MVI  B, 224
DLN_1:  MVI  M, 80H
        MOV  A, L
        ADI  32
        MOV  L, A
        JNC  DLN_2
        INR  H
DLN_2:  DCR  B
        JNZ  DLN_1
        RET

; A = BCD byte, D = col, E = x : two digits, D advances by 16
DRAWBCD:
        PUSH PSW
        RRC
        RRC
        RRC
        RRC
        ANI  0FH
        ADI  '0'
        PUSH D
        CALL DRAWCH
        POP  D
        MOV  A, D
        ADI  8
        MOV  D, A
        POP  PSW
        ANI  0FH
        ADI  '0'
        PUSH D
        CALL DRAWCH
        POP  D
        MOV  A, D
        ADI  8
        MOV  D, A
        RET

; HL = 0-terminated string, D = col, E = x (multiple of 8)
DRAWSTR:
        MOV  A, M
        ORA  A
        RZ
        PUSH H
        PUSH D
        CALL DRAWCH
        POP  D
        POP  H
        INX  H
        MOV  A, D
        ADI  8
        MOV  D, A
        JMP  DRAWSTR

; A = ASCII (20h-5Fh), D = col, E = x
DRAWCH:
        SUI  20H
        MOV  L, A
        MVI  H, 0
        DAD  H
        DAD  H
        DAD  H
        PUSH B
        LXI  B, FONT
        DAD  B
        POP  B
        MVI  B, 8
        JMP  DRAWSPR

; =====================================================================
; Video primitives
; =====================================================================
; D = col, E = x  ->  HL = VRAM address, A = x & 7
CALCADDR:
        MOV  L, D
        MVI  H, 0
        DAD  H
        DAD  H
        DAD  H
        DAD  H
        DAD  H
        MOV  A, E
        RRC
        RRC
        RRC
        ANI  1FH
        ORA  L
        MOV  L, A
        MOV  A, H
        ADI  HIGH(VRAM)
        MOV  H, A
        MOV  A, E
        ANI  7
        RET

; HL = sprite, D = col, E = x (multiple of 8), B = width : overwrite
DRAWSPR:
        PUSH H
        CALL CALCADDR
        POP  D
DRAWCOLS:
        LDAX D
        MOV  M, A
        INX  D
        MOV  A, L
        ADI  32
        MOV  L, A
        JNC  DC_1
        INR  H
DC_1:   DCR  B
        JNZ  DRAWCOLS
        RET

; D = col, E = x, B = width : clear
CLRSPR:
        CALL CALCADDR
CC_1:   MVI  M, 0
        MOV  A, L
        ADI  32
        MOV  L, A
        JNC  CC_2
        INR  H
CC_2:   DCR  B
        JNZ  CC_1
        RET

; A = pattern, C = shift -> D = low byte, E = high byte (uses the shift hardware)
SHIFTPAT:
        MOV  B, A
        MOV  A, C
        OUT  2
        XRA  A
        OUT  4
        OUT  4
        MOV  A, B
        OUT  4
        IN   3
        MOV  D, A
        XRA  A
        OUT  4
        IN   3
        MOV  E, A
        RET

; B = col, C = x, A = pattern -> HL = address, D/E = pattern bytes
PREPBITS:
        PUSH PSW
        MOV  D, B
        MOV  E, C
        CALL CALCADDR
        MOV  C, A
        POP  PSW
        JMP  SHIFTPAT

; Z = no pixel under pattern
TESTBITS:
        MOV  A, M
        ANA  D
        MOV  B, A
        INX  H
        MOV  A, M
        ANA  E
        DCX  H
        ORA  B
        RET

ORBITS: MOV  A, M
        ORA  D
        MOV  M, A
        INX  H
        MOV  A, M
        ORA  E
        MOV  M, A
        DCX  H
        RET

CLRBITS:
        MOV  A, D
        CMA
        ANA  M
        MOV  M, A
        INX  H
        MOV  A, E
        CMA
        ANA  M
        MOV  M, A
        DCX  H
        RET

CLS:    LXI  H, VRAM
CLS_1:  MVI  M, 0
        INX  H
        MOV  A, H
        CPI  40H
        JNZ  CLS_1
        RET

; clear play field (x = 16..223) in every column
CLRFIELD:
        LXI  H, VRAM
        MVI  C, 224
CF_1:   PUSH H
        INX  H
        INX  H
        MVI  B, 26
CF_2:   MVI  M, 0
        INX  H
        DCR  B
        JNZ  CF_2
        POP  H
        LXI  D, 32
        DAD  D
        DCR  C
        JNZ  CF_1
        RET

; =====================================================================
; Misc
; =====================================================================
WAITF:  LDA  FRAMEFLG
        ORA  A
        JZ   WAITF
        XRA  A
        STA  FRAMEFLG
        RET

; B = frames
WAITN:  PUSH B
        CALL WAITF
        POP  B
        DCR  B
        JNZ  WAITN
        RET

READIN: IN   1
        MOV  B, A
        LDA  INHELD
        CMA
        ANA  B
        STA  INNEW
        MOV  A, B
        STA  INHELD
        RET

; 16-bit Galois LFSR -> A
RAND:   LHLD RNGL
        MOV  A, H
        ORA  A
        RAR
        MOV  H, A
        MOV  A, L
        RAR
        MOV  L, A
        JNC  RN_1
        MOV  A, H
        XRI  0B4H
        MOV  H, A
RN_1:   SHLD RNGL
        LDA  FRAMECNT
        XRA  L
        RET

; =====================================================================
; Data
; =====================================================================
SPRTAB: DW   SPR_A0, SPR_A1, SPR_B0, SPR_B1, SPR_C0, SPR_C1
UFOPTS: DB   50H, 0, 0, 1, 50H, 1, 0, 3
UFOSTR: DW   STR_U50, STR_U100, STR_U150, STR_U300
STR_U50:  DB '50 ', 0
STR_U100: DB '100', 0
STR_U150: DB '150', 0
STR_U300: DB '300', 0
STR_TITLE: DB 'ALIEN RAID', 0
STR_PUSH:  DB 'PUSH START', 0
STR_SCORE: DB 'SCORE', 0
STR_HI:    DB 'HI-SCORE', 0
STR_OVER:  DB 'GAME OVER', 0
STR_P30:   DB '=30 POINTS', 0
STR_P20:   DB '=20 POINTS', 0
STR_P10:   DB '=10 POINTS', 0
STR_PMYS:  DB '=? MYSTERY', 0

        INCLUDE gfx.inc
