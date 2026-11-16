// =============================================================================
// Options > Graphics > Crowd: cuanto publico se dibuja.
//
// El publico de WoR tiene dos partes:
//   - 2D: los cuatro modelos de crowd_model_array (crowd1..crowd4). El motor los dibuja cada frame en
//     viewports aparte y su imagen se pega en las tarjetas del publico del fondo (create_crowd_models).
//   - 3D: el render batch "Crowd3D" (CreateCrowd3D), las filas de delante del escenario.
//
// re_crowd_level (lo escribe re-wor desde [graphics] crowd):
//   0 None    sin publico (se crea igual al cargar el escenario, pero no se dibuja)
//   1 Low     solo las filas 3D de delante
//   2 Medium  solo el publico del fondo (2D)
//   3 High    el original (las dos partes)
//
// NO se usa disable_crowd: si el escenario carga sin crear el publico, el motor lo busca igual y se cierra
// (Access Violation en sub_82406D60 al cargar z_punkstage con disable_crowd = 1).
// Ocultar/mostrar usa lo mismo que el script del juego hide_crowd_models: :hide/:unhide en los modelos 2D y
// visibility_mask = 1 en Crowd3D (guardando la que tenia para devolverla). Ese script tambien se envuelve,
// para que cuando el juego vuelva a mostrar el publico (cambios de camara) se respete esta opcion.
// =============================================================================

hook hide_crowd_models

int re_crowd_level = 3
int re_crowd_hid_2d = 0          // 1 = re-wor oculto los modelos 2D (y los tiene que devolver)
int re_crowd_hid_3d = 0          // 1 = re-wor oculto Crowd3D
int re_crowd_mask_3d = 4         // visibility_mask que tenia Crowd3D antes de ocultarlo

script hide_crowd_models
    re_orig_hide_crowd_models <...>
    re_crowd_apply
endscript

// 2D: ocultar o mostrar los modelos que se dibujan en las tarjetas
script re_crowd_2d show = 1
    crowd_models = ($crowd_model_array)
    if (<crowd_models> = none)
        return
    endif
    GetArraySize <crowd_models>
    if (<array_size> = 0)
        return
    endif
    i = 0
    begin
        objid = (<crowd_models> [ <i> ] .name)
        if ObjectExists id = <objid>
            if not StructureContains structure = (<crowd_models> [ <i> ]) remap_only
                if (<show> = 1)
                    <objid> :unhide
                else
                    <objid> :hide
                endif
            endif
        endif
        i = (<i> + 1)
    repeat <array_size>
endscript

// 3D: visibility_mask 1 = oculto (convencion de hide_crowd_models)
script re_crowd_3d show = 1
    GetRenderBatchSystemVisibility name = Crowd3D
    if (<show> = 1)
        if ($re_crowd_hid_3d = 1)
            if (<visibility_mask> = 1)
                SetRenderBatchSystemVisibility name = Crowd3D visibility_mask = ($re_crowd_mask_3d)
            endif
            Change re_crowd_hid_3d = 0
        endif
    else
        if (<visibility_mask> != 1)
            Change re_crowd_mask_3d = <visibility_mask>
            SetRenderBatchSystemVisibility name = Crowd3D visibility_mask = 1
        endif
        Change re_crowd_hid_3d = 1
    endif
endscript

script re_crowd_apply
    level = ($re_crowd_level)
    // 2D: Medium y High
    if (<level> >= 2)
        if ($re_crowd_hid_2d = 1)
            re_crowd_2d show = 1
            Change re_crowd_hid_2d = 0
        endif
    else
        re_crowd_2d show = 0
        Change re_crowd_hid_2d = 1
    endif
    // 3D: Low y High
    if (<level> = 1)
        re_crowd_3d show = 1
    elseif (<level> = 3)
        re_crowd_3d show = 1
    else
        re_crowd_3d show = 0
    endif
endscript
