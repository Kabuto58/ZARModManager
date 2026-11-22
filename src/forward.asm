; ZALoader: the 17 exports of the real version.dll, each a jump to the System32 function DllMain resolved.
; za_version_orig is filled in DllMain (loader.cpp), in the order of kVersionExports.
; Also za_ability_stub, the input hook's stub (system.inl).

EXTERN za_version_orig:QWORD
EXTERN za_update_input:QWORD
EXTERN za_after_input_update:PROC

FORWARD MACRO name, index
name PROC
    jmp QWORD PTR [za_version_orig + index * 8]
name ENDP
ENDM

.code
FORWARD Fwd_GetFileVersionInfoA, 0
FORWARD Fwd_GetFileVersionInfoByHandle, 1
FORWARD Fwd_GetFileVersionInfoExA, 2
FORWARD Fwd_GetFileVersionInfoExW, 3
FORWARD Fwd_GetFileVersionInfoSizeA, 4
FORWARD Fwd_GetFileVersionInfoSizeExA, 5
FORWARD Fwd_GetFileVersionInfoSizeExW, 6
FORWARD Fwd_GetFileVersionInfoSizeW, 7
FORWARD Fwd_GetFileVersionInfoW, 8
FORWARD Fwd_VerFindFileA, 9
FORWARD Fwd_VerFindFileW, 10
FORWARD Fwd_VerInstallFileA, 11
FORWARD Fwd_VerInstallFileW, 12
FORWARD Fwd_VerLanguageNameA, 13
FORWARD Fwd_VerLanguageNameW, 14
FORWARD Fwd_VerQueryValueA, 15
FORWARD Fwd_VerQueryValueW, 16

; Reached (through a jump in GameAssembly.dll) from the game's one call to CharacterInputController.UpdateInput, in
; place of that call: calls UpdateInput with the stack exactly as the game set it up (its return address is popped
; and the call pushes ours in the same slot), then za_after_input_update(controller), then returns to the game.
.data
stubRet  DQ 0
stubThis DQ 0
.code
za_ability_stub PROC
    pop r11
    mov stubRet, r11
    mov stubThis, rcx
    call QWORD PTR [za_update_input]
    sub rsp, 20h
    mov rcx, stubThis
    call za_after_input_update
    add rsp, 20h
    push stubRet
    ret
za_ability_stub ENDP
END
