// =============================================================================
// Options > Graphics: opciones graficas de PC dentro del menu de opciones del juego.
//
// Se compila con tools/qbc.py (CMake lo hace al compilar re-wor) y re-wor lo registra al arrancar.
// Cada opcion es una global entera re_gfx_v<N>; re-wor (gfx_options.cpp, SyncGameMenu) la lee, la
// guarda en [graphics] de re_wor_de.toml y aplica al momento lo que se puede aplicar en caliente.
// El orden de las opciones es el mismo que en SyncGameMenu: si se cambia aqui, se cambia alli.
// =============================================================================

// Los originales pasan a llamarse re_orig_<nombre>:
//  - ui_create_options: el menu de opciones, al que se anade la entrada Graphics.
//  - options_facebook: la pantalla de Facebook (servicio cerrado) presta su estado de interfaz a la
//    pantalla Graphics. Solo cuando la abre la entrada Graphics (re_gfx_active = 1); si no, se comporta
//    como siempre. Asi la pantalla usa la pila de menus del juego (atras, sonidos, ayuda) sin registrar
//    un estado nuevo en el motor.
hook ui_create_options ui_create_options_facebook ui_destroy_options_facebook ui_return_options_facebook

int re_gfx_v0 = 0      // resolucion
int re_gfx_v1 = 1      // modo de pantalla
int re_gfx_v2 = 0      // frecuencia (FPS del juego)
int re_gfx_v3 = 0      // VSync del monitor
int re_gfx_v4 = 0      // contador de FPS
int re_gfx_v5 = 0      // resolucion de render
int re_gfx_v6 = 1      // antialiasing
int re_gfx_v7 = 0      // escalado
int re_gfx_v8 = 0      // nitidez
int re_gfx_v9 = 0      // bloom
int re_gfx_v10 = 0     // glow
int re_gfx_v11 = 0     // blur
int re_gfx_v12 = 5     // brillo (5 = neutro)
int re_gfx_v13 = 5     // contraste
int re_gfx_v14 = 5     // saturacion
int re_gfx_v15 = 0     // vineta
int re_gfx_v16 = 0     // grano
int re_gfx_active = 0  // 1 = el estado de options_facebook muestra la pantalla Graphics
int re_gfx_reset = 0   // 1 = valores por defecto (re-wor lo vuelve a 0)
int re_gfx_pending = 0 // 1 = hay cambios que se aplican al reiniciar (lo escribe re-wor)

// ---------------------------------------------------------------- menu de opciones: nueva entrada
script ui_create_options
    Change re_gfx_active = 0
    re_orig_ui_create_options <...>
    if ScreenElementExists id = options_menu_id
        FormatText textname = text "Graphics"
        mainmenu_setup_option {
            choose_script = re_gfx_open
            back_script = ui_options_pad_back_script
            menu_item_text = <text>
            screen_id = options_menu_id
            helper_text = qs(0x03ac90f0)
        }
        main_menu_finalize
    endif
endscript

script re_gfx_open
    Change re_gfx_active = 1
    generic_event_choose state = UIstate_options_facebook
endscript

script ui_create_options_facebook
    if ($re_gfx_active = 1)
        re_gfx_create_screen
    else
        re_orig_ui_create_options_facebook <...>
    endif
endscript

script ui_destroy_options_facebook
    if ScreenElementExists id = re_gfx_menu_id
        re_gfx_destroy_screen
    else
        re_orig_ui_destroy_options_facebook <...>
    endif
endscript

script ui_return_options_facebook
    if ($re_gfx_active = 0)
        re_orig_ui_return_options_facebook <...>
    endif
endscript

// ---------------------------------------------------------------- pantalla
script re_gfx_create_screen
    Change rich_presence_context = presence_menus
    FormatText textname = title "Graphics"
    UIStack_CreateBoundScreenElement {
        parent = root_window
        id = re_gfx_menu_id
        type = DescInterface
        desc = "mainmenu"
        tags = { }
        event_handlers = [ { pad_back re_gfx_back } ]
        exclusive_device = ($primary_controller)
        menu_header_cont_alpha = 1
        menu_header_text_text = <title>
    }
    mainmenu_setup_physics_elements screen_id = re_gfx_menu_id
    UIStack_SpawnBoundScriptNow fg_objects_1 params = { desc_id = re_gfx_menu_id }
    UIStack_SpawnBoundScriptNow ampzilla_anim params = { desc_id = re_gfx_menu_id }
    UIStack_SpawnBoundScriptNow anim_clouds params = { desc_id = re_gfx_menu_id }
    i = 0
    begin
        re_gfx_row_text opt = <i>
        mainmenu_setup_option {
            choose_script = re_gfx_change
            choose_params = { opt = <i> dir = 1 }
            back_script = re_gfx_back
            menu_item_text = <text>
            screen_id = re_gfx_menu_id
            helper_text = qs(0x03ac90f0)
        }
        <mainmenu_item_id> :SE_SetProps event_handlers = [
            { pad_left re_gfx_change params = { opt = <i> dir = -1 } }
            { pad_right re_gfx_change params = { opt = <i> dir = 1 } }
        ]
        i = (<i> + 1)
    repeat 18
    FormatText textname = text "*  Applies after restarting the game"
    mainmenu_setup_option menu_item_text = <text> screen_id = re_gfx_menu_id helper_text = qs(0x03ac90f0) not_focusable
    main_menu_finalize
    LaunchEvent type = focus target = current_menu data = { child_index = 0 }
    add_gamertag_helper exclusive_device = ($primary_controller)
    add_user_control_helper text = qs(0xc18d5e76) button = green z = 100000 all_buttons
    add_user_control_helper text = qs(0xaf4d5dd2) button = red z = 100000 all_buttons
endscript

script re_gfx_destroy_screen
    Change re_gfx_active = 0
    clean_up_user_control_helpers
    if ScreenElementExists id = re_gfx_menu_id
        DestroyScreenElement id = re_gfx_menu_id
    endif
endscript

script re_gfx_back
    UI_SFX menustate = generic uitype = back
    generic_event_back
endscript

// ---------------------------------------------------------------- opciones
// devuelve label, kind (0 lista, 1 numero, 2 accion), values, value, count, offset, restart
script re_gfx_info opt = 0
    kind = 0
    restart = 0
    offset = 0
    values = [ "" ]
    value = 0
    switch <opt>
        case 0
            label = "Resolution"
            values = [ "Desktop" "1280 x 720" "1920 x 1080" "2560 x 1440" "3840 x 2160" ]
            value = ($re_gfx_v0)
            restart = 1
        case 1
            label = "Display Mode"
            values = [ "Windowed" "Borderless" "Fullscreen" ]
            value = ($re_gfx_v1)
            restart = 1
        case 2
            label = "Frame Rate"
            values = [ "60 FPS" "75 FPS" "120 FPS" "144 FPS" "165 FPS" "240 FPS" ]
            value = ($re_gfx_v2)
            restart = 1
        case 3
            label = "VSync"
            values = [ "Off" "On" ]
            value = ($re_gfx_v3)
        case 4
            label = "Show FPS"
            values = [ "Off" "On" ]
            value = ($re_gfx_v4)
        case 5
            label = "Render Resolution"
            values = [ "1x  (720p)" "2x  (1440p)" "3x  (2160p)" "4x  (2880p)" ]
            value = ($re_gfx_v5)
            restart = 1
        case 6
            label = "Anti-Aliasing"
            values = [ "Off" "MSAA 2x" ]
            value = ($re_gfx_v6)
            restart = 1
        case 7
            label = "Upscaling"
            values = [ "Bilinear" "AMD CAS" "AMD FSR 1" ]
            value = ($re_gfx_v7)
            restart = 1
        case 8
            label = "Sharpness"
            kind = 1
            value = ($re_gfx_v8)
            restart = 1
        case 9
            label = "Bloom"
            kind = 1
            value = ($re_gfx_v9)
        case 10
            label = "Glow"
            kind = 1
            value = ($re_gfx_v10)
        case 11
            label = "Blur"
            kind = 1
            value = ($re_gfx_v11)
        case 12
            label = "Brightness"
            kind = 1
            offset = -5
            value = ($re_gfx_v12)
        case 13
            label = "Contrast"
            kind = 1
            offset = -5
            value = ($re_gfx_v13)
        case 14
            label = "Saturation"
            kind = 1
            offset = -5
            value = ($re_gfx_v14)
        case 15
            label = "Vignette"
            kind = 1
            value = ($re_gfx_v15)
        case 16
            label = "Film Grain"
            kind = 1
            value = ($re_gfx_v16)
        default
            label = "Reset to Defaults"
            kind = 2
    endswitch
    count = 11
    if (<kind> = 0)
        GetArraySize <values>
        count = <array_size>
    endif
    return label = <label> kind = <kind> values = <values> value = <value> count = <count> offset = <offset> restart = <restart>
endscript

script re_gfx_set opt = 0 value = 0
    switch <opt>
        case 0
            Change re_gfx_v0 = <value>
        case 1
            Change re_gfx_v1 = <value>
        case 2
            Change re_gfx_v2 = <value>
        case 3
            Change re_gfx_v3 = <value>
        case 4
            Change re_gfx_v4 = <value>
        case 5
            Change re_gfx_v5 = <value>
        case 6
            Change re_gfx_v6 = <value>
        case 7
            Change re_gfx_v7 = <value>
        case 8
            Change re_gfx_v8 = <value>
        case 9
            Change re_gfx_v9 = <value>
        case 10
            Change re_gfx_v10 = <value>
        case 11
            Change re_gfx_v11 = <value>
        case 12
            Change re_gfx_v12 = <value>
        case 13
            Change re_gfx_v13 = <value>
        case 14
            Change re_gfx_v14 = <value>
        case 15
            Change re_gfx_v15 = <value>
        case 16
            Change re_gfx_v16 = <value>
    endswitch
endscript

script re_gfx_row_text opt = 0
    re_gfx_info opt = <opt>
    if (<kind> = 1)
        FormatText textname = text "%a:   %d" a = <label> d = (<value> + <offset>)
    elseif (<kind> = 0)
        FormatText textname = text "%a:   %b" a = <label> b = (<values> [ <value> ])
    else
        FormatText textname = text "%a" a = <label>
    endif
    if (<restart> = 1)
        FormatText textname = text "%t  *" t = <text>
    endif
    return text = <text>
endscript

// izquierda / derecha / aceptar sobre una fila
script re_gfx_change opt = 0 dir = 1
    re_gfx_info opt = <opt>
    if (<kind> = 2)
        Change re_gfx_reset = 1
        UI_SFX menustate = generic uitype = select
        SpawnScriptNow re_gfx_refresh_after_reset
        return
    endif
    v = (<value> + <dir>)
    if (<kind> = 0)
        // las listas dan la vuelta
        if (<v> < 0)
            v = (<count> - 1)
        endif
        if (<v> >= <count>)
            v = 0
        endif
    else
        if (<v> < 0)
            v = 0
        endif
        if (<v> > 10)
            v = 10
        endif
    endif
    re_gfx_set opt = <opt> value = <v>
    re_gfx_refresh_row opt = <opt>
    UI_SFX menustate = generic uitype = select
endscript

script re_gfx_refresh_row opt = 0
    if not ScreenElementExists id = re_gfx_menu_id
        return
    endif
    if GetScreenElementChildren id = current_menu
        re_gfx_row_text opt = <opt>
        (<children> [ <opt> ]) :SE_SetProps menurow_txt_item_text = <text>
    endif
endscript

// re-wor pone los valores por defecto y devuelve re_gfx_reset a 0; despues se redibujan las filas
script re_gfx_refresh_after_reset
    begin
        if ($re_gfx_reset = 0)
            break
        endif
        Wait 1 gameframe
    repeat 60
    i = 0
    begin
        re_gfx_refresh_row opt = <i>
        i = (<i> + 1)
    repeat 17
endscript
