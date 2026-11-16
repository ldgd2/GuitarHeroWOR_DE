// =============================================================================
// Options > Controls: lobby de 4 jugadores, instrumento, deteccion de dispositivo y mapeo.
//
// Lo que se ve es un panel a pantalla completa (re_ctl_panel) hecho con fuentes, iconos e imagenes del
// juego. Debajo, invisible, va el menu del juego (desc "mainmenu" en options_facebook, re_gfx_active = 2):
// recibe arriba/abajo/aceptar/atras de cualquier control y pone los sonidos; cada opcion suya avisa con
// re_ctl_hl al ganar el foco y la pieza del panel con ese numero se ilumina.
//
// Las piezas del panel (re_ctl_items) se dibujan a medida: cada vista coloca y rellena las que necesita
// (re_ctl_item crea las que aun no existan) y oculta el resto. Mientras la pantalla esta abierta no se borra
// ninguna (el juego borra al final del frame y crear y borrar a la vez se lleva lo nuevo).
//   primero las opciones elegibles (mismo numero que la opcion del menu invisible), luego los adornos:
//   titulos de seccion, textos, la imagen grande del instrumento (40 como mucho)
// Cada pieza: [0] fondo / barra de seleccion, [1] texto a la izquierda, [2] texto centrado,
// [3] subtexto centrado, [4] icono, [5] imagen.
// Estilos: 0 fila, 1 tarjeta (imagen + titulo + subtitulo), 2 boton, 3 titulo de seccion, 4 texto gris,
// 5 imagen sola.
//
// Los pasos los lleva re-wor (src/de/controls_menu.cpp), que llama a re_ctl_begin / re_ctl_add /
// re_ctl_item / re_ctl_end / re_ctl_focus y cambia textos con re_ctl_itemtext. La pantalla solo le cuenta
// lo que se pulsa en re_ctl_cmd: opcion + 1 = aceptar, 1000 = atras, 2000 + opcion = izquierda,
// 3000 + opcion = derecha.
// =============================================================================

int re_ctl_active = 0   // 1 = la pantalla esta abierta
int re_ctl_session = 0  // sube cada vez que se abre (re-wor rellena de nuevo)
int re_ctl_cmd = 0      // ultima pulsacion (re-wor la lee y la pone a 0)
int re_ctl_gen = 0      // generacion de opciones del menu invisible: re_ctl_end quita las de vistas anteriores
int re_ctl_hl_idx = -1  // opcion con el foco

script re_ctl_open
    Change re_gfx_active = 2
    generic_event_choose state = UIstate_options_facebook
endscript

script re_ctl_create_screen
    Change rich_presence_context = presence_menus
    killspawnedscript name = mainmenu_freeplay_timer_pause
    killspawnedscript name = mainmenu_freeplay_timer
    FormatText textname = title "Controls"
    UIStack_CreateBoundScreenElement {
        parent = root_window
        id = re_ctl_menu_id
        type = DescInterface
        desc = "mainmenu"
        tags = { }
        event_handlers = [ { pad_back re_ctl_back } ]
        exclusive_device = ($primary_controller)
        menu_header_cont_alpha = 1
        menu_header_text_text = <title>
    }
    // sin esto mainmenu_setup_option lee un puntero vacio y el juego se cae
    mainmenu_setup_physics_elements screen_id = re_ctl_menu_id
    // opcion provisional con foco: atras funciona aunque re-wor aun no haya puesto las suyas
    FormatText textname = text "..."
    mainmenu_setup_option {
        choose_script = re_ctl_back
        back_script = re_ctl_back
        menu_item_text = <text>
        screen_id = re_ctl_menu_id
        helper_text = qs(0x03ac90f0)
    }
    Change re_ctl_gen = 0
    <mainmenu_item_id> :Obj_SetIntegerTag tag_name = re_ctl_gen integer_value = 0
    main_menu_finalize
    LaunchEvent type = focus target = current_menu data = { child_index = 0 }
    // el menu del juego solo maneja la entrada: lo que se ve es el panel
    re_ctl_menu_id :SE_SetProps alpha = 0
    re_ctl_make_panel
    SpawnScriptNow re_ctl_guard
    add_gamertag_helper exclusive_device = ($primary_controller)
    add_user_control_helper text = qs(0xc18d5e76) button = green z = 100000 all_buttons
    add_user_control_helper text = qs(0xaf4d5dd2) button = red z = 100000 all_buttons
    Change re_ctl_cmd = 0
    Change re_ctl_hl_idx = -1
    Change re_ctl_session = ($re_ctl_session + 1)
    Change re_ctl_active = 1
endscript

script re_ctl_destroy_screen
    Change re_ctl_active = 0
    Change re_gfx_active = 0
    clean_up_user_control_helpers
    if ScreenElementExists id = re_ctl_panel
        DestroyScreenElement id = re_ctl_panel
    endif
    if ScreenElementExists id = re_ctl_menu_id
        DestroyScreenElement id = re_ctl_menu_id
    endif
endscript

// El panel no va dentro del menu del juego (esta invisible y el panel lo heredaria). Si el juego quita el
// menu por su cuenta (al salir o al cambiar de estado), el panel se va con el.
script re_ctl_guard
    SetSpawnInstanceLimits max = 1 management = kill_oldest
    begin
        Wait 1 gameframe
        if not ScreenElementExists id = re_ctl_menu_id
            break
        endif
    repeat
    if ScreenElementExists id = re_ctl_panel
        DestroyScreenElement id = re_ctl_panel
    endif
    Change re_ctl_active = 0
endscript

// ---------------------------------------------------------------- panel
// Coordenadas de pantalla del juego: 1280 x 720.
script re_ctl_make_panel
    CreateScreenElement {
        type = ContainerElement
        parent = root_window
        id = re_ctl_panel
        pos = (0, 0)
        just = [ left top ]
        z_priority = 50
    }
    CreateScreenElement {
        type = SpriteElement
        parent = re_ctl_panel
        texture = white
        dims = (1280, 720)
        pos = (640, 360)
        just = [ center center ]
        rgba = [ 12 9 8 255 ]
        alpha = 0.9
        z_priority = 50
    }
    CreateScreenElement {
        type = TextElement
        parent = re_ctl_panel
        id = re_ctl_title
        font = fontgrid_title_a1
        text = "Controls"
        scale = 0.7
        pos = (80, 62)
        just = [ left center ]
        rgba = [ 216 181 117 255 ]
        z_priority = 53
    }
    CreateScreenElement {
        type = SpriteElement
        parent = re_ctl_panel
        texture = white
        dims = (1120, 2)
        pos = (640, 96)
        just = [ center center ]
        rgba = [ 216 181 117 255 ]
        alpha = 0.7
        z_priority = 51
    }
    CreateScreenElement {
        type = ContainerElement
        parent = re_ctl_panel
        id = re_ctl_items
        pos = (0, 0)
        just = [ left top ]
        z_priority = 52
    }
endscript

// una pieza nueva al final de re_ctl_items: se crean solo cuando una vista las necesita y no se borran hasta
// salir de la pantalla
script re_ctl_new_item
    CreateScreenElement {
        type = ContainerElement
        parent = re_ctl_items
        pos = (0, 0)
        just = [ left top ]
        z_priority = 52
        alpha = 0
    }
    item = <id>
    CreateScreenElement {
        type = SpriteElement
        parent = <item>
        texture = white
        dims = (10, 10)
        pos = (0, 0)
        just = [ left top ]
        rgba = [ 40 33 27 255 ]
        alpha = 0
        z_priority = 52
    }
    CreateScreenElement {
        type = TextElement
        parent = <item>
        font = fontgrid_text_A1
        text = ""
        scale = 0.42
        pos = (0, 0)
        just = [ left center ]
        rgba = [ 235 230 220 255 ]
        z_priority = 55
    }
    CreateScreenElement {
        type = TextElement
        parent = <item>
        font = fontgrid_title_a1
        text = ""
        scale = 0.5
        pos = (0, 0)
        just = [ center center ]
        rgba = [ 235 230 220 255 ]
        z_priority = 55
    }
    CreateScreenElement {
        type = TextElement
        parent = <item>
        font = fontgrid_text_A1
        text = ""
        scale = 0.36
        pos = (0, 0)
        just = [ center center ]
        rgba = [ 190 182 170 255 ]
        z_priority = 55
    }
    CreateScreenElement {
        type = SpriteElement
        parent = <item>
        texture = icon_guitar_fill
        dims = (24, 24)
        pos = (0, 0)
        just = [ center center ]
        alpha = 0
        z_priority = 55
    }
    CreateScreenElement {
        type = SpriteElement
        parent = <item>
        texture = icon_guitar_fill
        dims = (10, 10)
        pos = (0, 0)
        just = [ center center ]
        alpha = 0
        z_priority = 54
    }
    <item> :Obj_SetIntegerTag tag_name = re_ctl_style integer_value = 0
endscript

// ---------------------------------------------------------------- pulsaciones (las atiende re-wor)
script re_ctl_pick idx = 0
    UI_SFX menustate = generic uitype = select
    Change re_ctl_cmd = (<idx> + 1)
endscript

script re_ctl_back
    Change re_ctl_cmd = 1000
    SpawnScriptNow re_ctl_back_fallback
endscript

// si re-wor no atiende el atras en 2 segundos, se sale igual (nunca se queda bloqueada la pantalla).
// En segundos y no en frames: a 240 FPS 30 frames son 0,125 s, menos de lo que re-wor tarda entre vistas.
script re_ctl_back_fallback
    Wait 2 seconds
    if ($re_ctl_cmd = 1000)
        Change re_ctl_cmd = 0
        re_ctl_exit_now
    endif
endscript

script re_ctl_side idx = 0 dir = 1
    if (<dir> < 0)
        Change re_ctl_cmd = (<idx> + 2000)
    else
        Change re_ctl_cmd = (<idx> + 3000)
    endif
endscript

// salir al menu de opciones (lo pide re-wor con atras o Listo en el lobby)
script re_ctl_exit
    SpawnScriptNow re_ctl_exit_now
endscript

script re_ctl_exit_now
    UI_SFX menustate = generic uitype = back
    generic_event_back
endscript

// Prueba: sale de Controls y toca una cancion al azar (facil) con el instrumento y el mando del jugador
script re_ctl_test part = guitar controller = 0
    SpawnScriptNow re_ctl_test_now params = { part = <part> controller = <controller> }
endscript

script re_ctl_test_now part = guitar controller = 0
    UI_SFX menustate = generic uitype = select
    generic_event_back
    Wait 1 seconds
    get_random_song
    autolaunch song = <song> difficulty = easy part = <part> controller = <controller> game_mode = quickplay no_setlist
    Wait 2 gameframe
    Change autolaunch_go = 1
endscript

// Sustituye al del juego (mismo nombre): el temporizador de inactividad del menu principal va pegado al
// propio menu, asi que killspawnedscript no lo para. Mientras Controls esta abierta no debe saltar a
// freeplay; al cerrarla vuelve a contar desde cero.
script mainmenu_freeplay_timer
    SetScriptCannotPause
    SetSpawnInstanceLimits max = 1 management = kill_oldest
    begin
        Wait ($g_mainmenu_freeplay_timer) seconds
        if ($re_ctl_active = 0)
            break
        endif
    repeat
    LaunchEvent type = unfocus target = current_menu
    main_menu_select_freeplay do_not_play_sfx kill_freeplay_timeout = 0
endscript

script re_ctl_keepalive
    killspawnedscript name = mainmenu_freeplay_timer_pause
    killspawnedscript name = mainmenu_freeplay_timer
endscript

script re_ctl_sfx_back
    UI_SFX menustate = generic uitype = back
endscript

script re_ctl_sfx_select
    UI_SFX menustate = generic uitype = select
endscript

// ---------------------------------------------------------------- vistas (las pone re-wor)
// re-wor llama a estos desde fuera de la pantalla; el trabajo con los elementos lo hace un script lanzado
// (SpawnScriptNow), que corre como cualquier otro script del juego y en el mismo orden.

// empieza una vista: titulo y todas las piezas ocultas
script re_ctl_begin title = "Controls"
    SpawnScriptNow re_ctl_begin_now params = { title = <title> }
endscript

script re_ctl_begin_now title = "Controls"
    if not ScreenElementExists id = re_ctl_menu_id
        return
    endif
    re_ctl_menu_id :SE_SetProps menu_header_text_text = <title>
    if ScreenElementExists id = re_ctl_title
        re_ctl_title :SE_SetProps text = <title>
    endif
    // las opciones viejas del menu invisible se quitan al final (re_ctl_end): nunca se queda vacio
    LaunchEvent type = unfocus target = current_menu
    Change re_ctl_gen = ($re_ctl_gen + 1)
    Change re_ctl_hl_idx = -1
    if GetScreenElementChildren id = re_ctl_items
        items = <children>
        GetArraySize <items>
        i = 0
        begin
            (<items> [ <i> ]) :SE_SetProps alpha = 0
            i = (<i> + 1)
        repeat <array_size>
    endif
endscript

// opciones elegibles 0..n-1 del menu invisible, en orden y en un solo script (las piezas se ponen con
// re_ctl_item); el orden de las opciones es el de arriba/abajo y el de child_index
script re_ctl_add n = 0
    SpawnScriptNow re_ctl_add_now_all params = { n = <n> }
endscript

script re_ctl_add_now_all n = 0
    i = 0
    begin
        if not (<i> < <n>)
            break
        endif
        re_ctl_add_now idx = <i>
        i = (<i> + 1)
    repeat
endscript

script re_ctl_add_now idx = 0
    if not ScreenElementExists id = re_ctl_menu_id
        return
    endif
    FormatText textname = text "-"
    mainmenu_setup_option {
        choose_script = re_ctl_pick
        choose_params = { idx = <idx> }
        back_script = re_ctl_back
        menu_item_text = <text>
        screen_id = re_ctl_menu_id
        helper_text = qs(0x03ac90f0)
        additional_focus_script = re_ctl_hl
        additional_focus_params = { idx = <idx> }
    }
    <mainmenu_item_id> :SE_SetProps event_handlers = [
        { pad_left re_ctl_side params = { idx = <idx> dir = -1 } }
        { pad_right re_ctl_side params = { idx = <idx> dir = 1 } }
    ]
    <mainmenu_item_id> :Obj_SetIntegerTag tag_name = re_ctl_gen integer_value = ($re_ctl_gen)
endscript

// coloca y rellena la pieza idx del panel (x, y = esquina de arriba a la izquierda; w, h = tamano)
script re_ctl_item idx = 0 x = 0 y = 0 w = 100 h = 30 style = 0 text = "" sub = "" imgw = 0
    SpawnScriptNow re_ctl_item_now params = { <...> }
endscript

script re_ctl_item_now idx = 0 x = 0 y = 0 w = 100 h = 30 style = 0 text = "" sub = "" imgw = 0
    if not ScreenElementExists id = re_ctl_items
        return
    endif
    // sin hijos GetScreenElementChildren falla: el panel aun no tiene piezas
    array_size = 0
    if GetScreenElementChildren id = re_ctl_items
        GetArraySize <children>
    endif
    if not (<idx> < <array_size>)
        if (<idx> > 39)
            return
        endif
        n = (<idx> - <array_size> + 1)
        begin
            re_ctl_new_item
        repeat <n>
        if not GetScreenElementChildren id = re_ctl_items
            return
        endif
    endif
    items = <children>
    item = (<items> [ <idx> ])
    if not GetScreenElementChildren id = <item>
        return
    endif
    p = <children>
    <item> :SE_SetProps pos = (((1.0, 0.0) * <x>) + ((0.0, 1.0) * <y>)) alpha = 1
    <item> :Obj_SetIntegerTag tag_name = re_ctl_style integer_value = <style>
    size = (((1.0, 0.0) * <w>) + ((0.0, 1.0) * <h>))
    mid_y = ((0.0, 0.5) * <h>)
    center = (((0.5, 0.0) * <w>) + ((0.0, 0.5) * <h>))
    // apagado todo; cada estilo enciende lo suyo
    (<p> [ 0 ]) :SE_SetProps dims = <size> alpha = 0 rgba = [ 40 33 27 255 ]
    (<p> [ 1 ]) :SE_SetProps text = "" rgba = [ 235 230 220 255 ]
    (<p> [ 2 ]) :SE_SetProps text = "" rgba = [ 235 230 220 255 ]
    (<p> [ 3 ]) :SE_SetProps text = ""
    (<p> [ 4 ]) :SE_SetProps alpha = 0
    (<p> [ 5 ]) :SE_SetProps alpha = 0
    switch <style>
        case 0
            // fila: barra (solo con el foco), icono opcional y texto a la izquierda
            tx = 12
            if GotParam icon
                (<p> [ 4 ]) :SE_SetProps texture = <icon> alpha = 1 pos = (((1.0, 0.0) * 22) + <mid_y>)
                tx = 44
            endif
            (<p> [ 1 ]) :SE_SetProps text = <text> scale = 0.42 pos = (((1.0, 0.0) * <tx>) + <mid_y>)
        case 1
            // tarjeta: fondo, imagen grande, titulo y subtitulo
            (<p> [ 0 ]) :SE_SetProps alpha = 0.75
            if GotParam img
                (<p> [ 5 ]) :SE_SetProps texture = <img> alpha = 1 dims = (((1.0, 1.0) * <imgw>)) pos = (((0.5, 0.0) * <w>) + ((0.0, 0.42) * <h>))
            endif
            (<p> [ 2 ]) :SE_SetProps text = <text> scale = 0.4 pos = (((0.5, 0.0) * <w>) + ((0.0, 1.0) * <h>) + (0.0, -58.0))
            (<p> [ 3 ]) :SE_SetProps text = <sub> scale = 0.33 pos = (((0.5, 0.0) * <w>) + ((0.0, 1.0) * <h>) + (0.0, -26.0))
        case 2
            // boton
            (<p> [ 0 ]) :SE_SetProps alpha = 0.85
            (<p> [ 2 ]) :SE_SetProps text = <text> scale = 0.5 pos = <center>
        case 3
            // titulo de seccion (dorado) con su raya
            (<p> [ 1 ]) :SE_SetProps text = <text> scale = 0.5 rgba = [ 216 181 117 255 ] pos = <mid_y>
            (<p> [ 0 ]) :SE_SetProps dims = (((1.0, 0.0) * <w>) + (0.0, 2.0)) pos = ((0.0, 1.0) * <h>) rgba = [ 216 181 117 255 ] alpha = 0.45
        case 4
            // texto gris
            (<p> [ 1 ]) :SE_SetProps text = <text> scale = 0.4 rgba = [ 165 158 148 255 ] pos = <mid_y>
        case 5
            // imagen sola, con su texto debajo
            if GotParam img
                (<p> [ 5 ]) :SE_SetProps texture = <img> alpha = 1 dims = <size> pos = <center>
            endif
            (<p> [ 2 ]) :SE_SetProps text = <text> scale = 0.4 pos = (((0.5, 0.0) * <w>) + ((0.0, 1.0) * <h>) + (0.0, 26.0))
            (<p> [ 3 ]) :SE_SetProps text = <sub> scale = 0.33 pos = (((0.5, 0.0) * <w>) + ((0.0, 1.0) * <h>) + (0.0, 54.0))
    endswitch
    if (<idx> = ($re_ctl_hl_idx))
        re_ctl_paint item = <item> on = 1
    endif
endscript

// cambia solo el texto de una pieza (esperas de tecla, nivel del microfono...)
script re_ctl_itemtext idx = 0 text = ""
    SpawnScriptNow re_ctl_itemtext_now params = { idx = <idx> text = <text> }
endscript

script re_ctl_itemtext_now idx = 0 text = ""
    if not GetScreenElementChildren id = re_ctl_items
        return
    endif
    items = <children>
    GetArraySize <items>
    if not (<idx> < <array_size>)
        return
    endif
    if GetScreenElementChildren id = (<items> [ <idx> ])
        integer_value = 0
        (<items> [ <idx> ]) :Obj_GetIntegerTag tag_name = re_ctl_style
        if (<integer_value> = 0)
            (<children> [ 1 ]) :SE_SetProps text = <text>
        elseif (<integer_value> = 3)
            (<children> [ 1 ]) :SE_SetProps text = <text>
        elseif (<integer_value> = 4)
            (<children> [ 1 ]) :SE_SetProps text = <text>
        else
            (<children> [ 2 ]) :SE_SetProps text = <text>
        endif
    endif
endscript

// pinta una pieza con foco (on = 1) o sin el
script re_ctl_paint item = none on = 0
    if not GetScreenElementChildren id = <item>
        return
    endif
    integer_value = 0
    <item> :Obj_GetIntegerTag tag_name = re_ctl_style
    style = <integer_value>
    if (<on> = 1)
        (<children> [ 0 ]) :SE_SetProps rgba = [ 216 181 117 255 ] alpha = 0.92
        (<children> [ 1 ]) :SE_SetProps rgba = [ 22 17 12 255 ]
        (<children> [ 2 ]) :SE_SetProps rgba = [ 22 17 12 255 ]
        (<children> [ 3 ]) :SE_SetProps rgba = [ 50 40 30 255 ]
    else
        (<children> [ 0 ]) :SE_SetProps rgba = [ 40 33 27 255 ]
        if (<style> = 0)
            (<children> [ 0 ]) :SE_SetProps alpha = 0
        elseif (<style> = 1)
            (<children> [ 0 ]) :SE_SetProps alpha = 0.75
        else
            (<children> [ 0 ]) :SE_SetProps alpha = 0.85
        endif
        (<children> [ 1 ]) :SE_SetProps rgba = [ 235 230 220 255 ]
        (<children> [ 2 ]) :SE_SetProps rgba = [ 235 230 220 255 ]
        (<children> [ 3 ]) :SE_SetProps rgba = [ 190 182 170 255 ]
    endif
endscript

// la opcion idx gana el foco (lo avisa el menu del juego)
script re_ctl_hl idx = 0
    if not GetScreenElementChildren id = re_ctl_items
        return
    endif
    items = <children>
    old = ($re_ctl_hl_idx)
    if (<old> > -1)
        if (<old> < 24)
            re_ctl_paint item = (<items> [ <old> ]) on = 0
        endif
    endif
    Change re_ctl_hl_idx = <idx>
    if (<idx> < 24)
        re_ctl_paint item = (<items> [ <idx> ]) on = 1
    endif
endscript

// cierra la vista: quita las opciones de vistas anteriores del menu invisible
script re_ctl_end
    SpawnScriptNow re_ctl_end_now
endscript

script re_ctl_end_now
    if not ScreenElementExists id = re_ctl_menu_id
        return
    endif
    gen = ($re_ctl_gen)
    if GetScreenElementChildren id = current_menu
        GetArraySize <children>
        if (<array_size> > 0)
            i = 0
            begin
                integer_value = -1
                (<children> [ <i> ]) :Obj_GetIntegerTag tag_name = re_ctl_gen
                if not (<integer_value> = <gen>)
                    DestroyScreenElement id = (<children> [ <i> ])
                endif
                i = (<i> + 1)
            repeat <array_size>
        endif
    endif
endscript

// unos frames despues de re_ctl_end, con las opciones viejas ya borradas
script re_ctl_focus focus = 0
    SpawnScriptNow re_ctl_focus_now params = { focus = <focus> }
endscript

script re_ctl_focus_now focus = 0
    if not ScreenElementExists id = re_ctl_menu_id
        return
    endif
    if GetScreenElementChildren id = current_menu
        main_menu_finalize
        LaunchEvent type = unfocus target = current_menu
        LaunchEvent type = focus target = current_menu data = { child_index = <focus> }
    endif
endscript

// ---------------------------------------------------------------- aviso "no tienes controles"
// re-wor lo pide cada pocos segundos mientras ningun jugador tiene ningun control (el teclado hace de mando
// del jugador 1); solo sale en el menu principal y una vez por cada vez que se queda sin controles.
int re_noctl_shown = 0

script re_noctl_reset
    Change re_noctl_shown = 0
endscript

script re_noctl_try heading = "" body = "" b1 = "" b2 = ""
    SpawnScriptNow re_noctl_try_now params = { heading = <heading> body = <body> b1 = <b1> b2 = <b2> }
endscript

script re_noctl_try_now heading = "" body = "" b1 = "" b2 = ""
    if ($re_noctl_shown = 1)
        return
    endif
    ui_event_get_top
    if not GotParam base_name
        return
    endif
    if not (<base_name> = "mainmenu")
        return
    endif
    Change re_noctl_shown = 1
    LaunchEvent type = unfocus target = current_menu
    create_dialog_box {
        heading_text = <heading>
        body_text = <body>
        player_device = ($primary_controller)
        back_button_script = re_noctl_later
        options = [
            { func = re_noctl_config text = <b1> }
            { func = re_noctl_later text = <b2> }
        ]
    }
    if GotParam menu_id
        AssignAlias id = <menu_id> alias = re_noctl_menu
        LaunchEvent type = focus target = re_noctl_menu
    endif
endscript

script re_noctl_config
    destroy_dialog_box
    UI_SFX menustate = generic uitype = select
    re_ctl_open
endscript

script re_noctl_later
    destroy_dialog_box
    UI_SFX menustate = generic uitype = back
    LaunchEvent type = focus target = current_menu
endscript
