// Recursos NATIVOS del juego que el hook (re-wor/src/de/native_fx.cpp) crea con el propio motor.
// Todos estan siempre en memoria (qb.pak + zones/global.pak): no hace falta cargar nada y no se cae.
// Particulas: estructuras globales gp_fx_* (scripts/fx/particles), con sus texturas en global.pak.
export const PFX_FAV = [
 {
  "id": "gp_fx_sparkfountain_sparks_01",
  "label": "Fuente de chispas"
 },
 {
  "id": "gp_fx_sparkfountain_big_sparks_01",
  "label": "Fuente de chispas grande"
 },
 {
  "id": "gp_fx_topsparks_waterfall_01",
  "label": "Cascada de chispas"
 },
 {
  "id": "gp_fx_topsparks_waterfallshort_01",
  "label": "Cascada de chispas corta"
 },
 {
  "id": "gp_fx_sparkshowersmall_sparks_01",
  "label": "Lluvia de chispas"
 },
 {
  "id": "gp_fx_groundsparks_big_02",
  "label": "Chispas en el suelo"
 },
 {
  "id": "gp_fx_widesparks_01",
  "label": "Abanico de chispas"
 },
 {
  "id": "gp_fx_spinnersparks_01",
  "label": "Chispas giratorias"
 },
 {
  "id": "gp_fx_sparx_01",
  "label": "Chisporroteo"
 },
 {
  "id": "gp_fx_fireplume_01",
  "label": "Columna de fuego"
 },
 {
  "id": "gp_fx_fireplume_bottom_01",
  "label": "Base de llamas"
 },
 {
  "id": "gp_fx_smallfireplume_01",
  "label": "Llamarada pequena"
 },
 {
  "id": "gp_fx_encore_pillarfire01",
  "label": "Pilar de fuego"
 },
 {
  "id": "gp_fx_epicstage_celebintro_bigpillarfire01",
  "label": "Pilar de fuego enorme"
 },
 {
  "id": "gp_fx_poolfire_01",
  "label": "Charco de fuego"
 },
 {
  "id": "gp_fx_houseband_encore_circlefire01",
  "label": "Anillo de fuego"
 },
 {
  "id": "gp_fx_ms_pyrofire_01",
  "label": "Pirotecnia"
 },
 {
  "id": "gp_fx_newsmoke_topsmokefire_01",
  "label": "Bola de fuego"
 },
 {
  "id": "gp_fx_dragonfire_01",
  "label": "Aliento de dragon"
 },
 {
  "id": "gp_fx_fsc_greysmoke_01",
  "label": "Humo gris"
 },
 {
  "id": "gp_fx_fsc_greysmoke_column_01",
  "label": "Columna de humo"
 },
 {
  "id": "gp_fx_pitsmoke_01",
  "label": "Humo bajo (niebla)"
 },
 {
  "id": "gp_fx_wallsmoke_01",
  "label": "Humo de pared"
 },
 {
  "id": "gp_fx_floorgerb_smoke_01",
  "label": "Humo de cañon"
 },
 {
  "id": "gp_p_momentsmoke_01",
  "label": "Humo de momento"
 },
 {
  "id": "gp_fx_floorgerb_sparks_01",
  "label": "Confeti de cañon"
 },
 {
  "id": "gp_fx_sparkshowersmall_multisparks_01",
  "label": "Confeti en lluvia"
 },
 {
  "id": "gp_fx_fireworks_burst_flower_01",
  "label": "Fuegos artificiales (flor)"
 },
 {
  "id": "gp_fx_fireworks_sparklerocket_01",
  "label": "Cohete de chispas"
 },
 {
  "id": "gp_fx_fireworks_crackletrail_01",
  "label": "Estela crepitante"
 },
 {
  "id": "gp_fx_forumencore_rocket_whitesparks_01",
  "label": "Cohete blanco"
 },
 {
  "id": "gp_fx_tesla_sparks_01",
  "label": "Bobina Tesla"
 },
 {
  "id": "gp_fx_tesla_flashes_01",
  "label": "Destellos Tesla"
 },
 {
  "id": "gp_fx_outro02_fingerelectric01",
  "label": "Electricidad"
 },
 {
  "id": "gp_fx_finalbattle_ampzilla_static_bodyelectric01",
  "label": "Descargas electricas"
 },
 {
  "id": "gp_fx_intro03_lightning01",
  "label": "Rayo"
 },
 {
  "id": "gp_fx_glow_01",
  "label": "Resplandor"
 },
 {
  "id": "gp_fx_wallglow_01",
  "label": "Brillo de pared"
 },
 {
  "id": "gp_fx_pyro_baseflashes_01",
  "label": "Destellos de pirotecnia"
 },
 {
  "id": "gp_fx_sparkshowersmall_lensflares_01",
  "label": "Destellos de lente"
 },
 {
  "id": "gp_fx_stars_01",
  "label": "Estrellas"
 },
 {
  "id": "gp_ambient_dust_01",
  "label": "Polvo en el aire"
 },
 {
  "id": "gp_godray_dust_02",
  "label": "Polvo en rayos de luz"
 },
 {
  "id": "gp_fx_clouds_01",
  "label": "Nubes"
 },
 {
  "id": "gp_fx_singledrips_01",
  "label": "Gotas"
 },
 {
  "id": "gp_fx_guitar_flares_01",
  "label": "Destellos de guitarra"
 },
 {
  "id": "gp_fx_guitar_fire_01",
  "label": "Guitarra en llamas"
 },
 {
  "id": "gp_fx_hw_shota_energyball01",
  "label": "Bola de energia"
 },
 {
  "id": "gp_fx_waterfall_rings_01",
  "label": "Anillos de energia"
 }
];
export const PFX_ALL = ["gp_ambient_dust_01", "gp_fx_ambientdrips_01", "gp_fx_backglow_06", "gp_fx_ballhit_01", "gp_fx_barbariantorchfire_01", "gp_fx_barbariantorchglow_01", "gp_fx_batsflare_01", "gp_fx_batsflash_01", "gp_fx_batslongglow_01", "gp_fx_batstreaks_01", "gp_fx_candleglows_06", "gp_fx_carrocks_01", "gp_fx_clouds_01", "gp_fx_clouds_02", "gp_fx_clouds_03", "gp_fx_clouds_04", "gp_fx_clouds_05", "gp_fx_clouds_06", "gp_fx_dragon_fireball_02", "gp_fx_dragon_frontglow_01", "gp_fx_dragon_smoke_01", "gp_fx_dragonfire_01", "gp_fx_dragonfire_glow_01", "gp_fx_dragonfire_glow_attach_01", "gp_fx_electricity_arm_01", "gp_fx_electricity_finger_glow_01", "gp_fx_electricity_finger_rings_01", "gp_fx_electricity_finger_sparks_01", "gp_fx_encore_pillarfire01", "gp_fx_encoreflash01_01", "gp_fx_encorerocketsparklesbig_01", "gp_fx_encorerockettopbig_01", "gp_fx_encoreupglow_01", "gp_fx_epicstage_celebintro_bigpillarfire01", "gp_fx_epicstage_celebintro_bigpillarfire02", "gp_fx_epicstage_celebintro_pillarfire01", "gp_fx_epicstage_celebintro_pillarglow01", "gp_fx_epicstage_encore_armfire01", "gp_fx_epicstage_encore_bodysparks01", "gp_fx_epicstage_encore_footfire01", "gp_fx_epicstage_encore_footsparks01", "gp_fx_epicstage_pillarfire01", "gp_fx_epicstage_pillarglow01", "gp_fx_fe_fireworks_sparklerocket_top_01", "gp_fx_fe_fireworks_sparklerocq", "gp_fx_festival_encore_snortl01", "gp_fx_festival_encore_snortr01", "gp_fx_festival_encore_spittle01", "gp_fx_festival_torchfirebig01", "gp_fx_festival_torchfiresmall01", "gp_fx_festival_torchglowbig01", "gp_fx_festival_torchglowsmall01", "gp_fx_finalbattle_ampzilla_attackflash01", "gp_fx_finalbattle_ampzilla_attackglow01", "gp_fx_finalbattle_ampzilla_attackglow02", "gp_fx_finalbattle_ampzilla_attackglow03", "gp_fx_finalbattle_ampzilla_attackrings01", "gp_fx_finalbattle_ampzilla_footdust01", "gp_fx_finalbattle_ampzilla_longattack01", "gp_fx_finalbattle_ampzilla_longrings01", "gp_fx_finalbattle_ampzilla_longtrail01", "gp_fx_finalbattle_ampzilla_slambits01", "gp_fx_finalbattle_ampzilla_slamdust01", "gp_fx_finalbattle_ampzilla_slamelectric01", "gp_fx_finalbattle_ampzilla_slamglow01", "gp_fx_finalbattle_ampzilla_slamsparks01", "gp_fx_finalbattle_ampzilla_static_armelectric01", "gp_fx_finalbattle_ampzilla_static_armelectric02", "gp_fx_finalbattle_ampzilla_static_armglow01", "gp_fx_finalbattle_ampzilla_static_armsparks01", "gp_fx_finalbattle_ampzilla_static_armsparks02", "gp_fx_finalbattle_ampzilla_static_backsmoke01", "gp_fx_finalbattle_ampzilla_static_bodyelectric01", "gp_fx_finalbattle_ampzilla_static_bodysparks01", "gp_fx_finalbattle_ampzilla_static_headelectric01", "gp_fx_finalbattle_ampzilla_static_headglow01", "gp_fx_finalbattle_ampzilla_static_headsparks01", "gp_fx_finalbattle_ampzilla_static_legelectric01", "gp_fx_finalbattle_ampzilla_static_legelectric02", "gp_fx_finalbattle_ampzilla_static_legglow01", "gp_fx_finalbattle_ampzilla_static_legsparks01", "gp_fx_finalbattle_ampzilla_static_shoulderelectric01", "gp_fx_finalbattle_ampzilla_static_shoulderglow01", "gp_fx_finalbattle_ampzilla_static_towerelectric01", "gp_fx_finalbattle_ampzilla_static_towerelectric02", "gp_fx_finalbattle_lavafire01", "gp_fx_finalbattle_lavasparks01", "gp_fx_fireplume_01", "gp_fx_fireplume_02", "gp_fx_fireplume_attach_01", "gp_fx_fireplume_bottom_01", "gp_fx_fireplume_bottom_attach_01", "gp_fx_fireworks_burst_flower_01", "gp_fx_fireworks_burst_flower_center_01", "gp_fx_fireworks_burst_flower_glow", "gp_fx_fireworks_crackletrail_01", "gp_fx_fireworks_sparklerocket_01", "gp_fx_fireworks_sparklerocket_smoke_02", "gp_fx_floorgerb_long_smallflashes_02", "gp_fx_floorgerb_long_smoke_02", "gp_fx_floorgerb_long_sparks_03", "gp_fx_floorgerb_long_sparks_04", "gp_fx_floorgerb_smallflashes_01", "gp_fx_floorgerb_smoke_01", "gp_fx_floorgerb_sparks_01", "gp_fx_floorgerb_sparks_02", "gp_fx_forumencore_rocket_orangesparkglow_01", "gp_fx_forumencore_rocket_orangesparks_01", "gp_fx_forumencore_rocket_top_01", "gp_fx_forumencore_rocket_whitesparkglow_01", "gp_fx_forumencore_rocket_whitesparks_01", "gp_fx_forumencore_rocket_whitetop_01", "gp_fx_fsc_bottomglow_01", "gp_fx_fsc_frontflash_01", "gp_fx_fsc_frontglow_01", "gp_fx_fsc_glowsmoke_01", "gp_fx_fsc_greysmoke_01", "gp_fx_fsc_greysmoke_02", "gp_fx_fsc_greysmoke_03", "gp_fx_fsc_greysmoke_column_01", "gp_fx_fsc_sparks_01", "gp_fx_glow_01", "gp_fx_glow_02", "gp_fx_glow_03", "gp_fx_glow_04", "gp_fx_glow_05", "gp_fx_groundsparks_big_02", "gp_fx_groundsparks_big_03", "gp_fx_groundsparks_insideglow_01", "gp_fx_groundsparks_topsmoke_01", "gp_fx_guitar_bits_01", "gp_fx_guitar_fire_01", "gp_fx_guitar_flares_01", "gp_fx_guitar_glow_01", "gp_fx_guitar_sparkles_02", "gp_fx_guitar_whisps_01", "gp_fx_guitlast_ball_01", "gp_fx_guitlast_flare_01", "gp_fx_guitlast_flash", "gp_fx_guitlast_flash_fast_01", "gp_fx_guitlast_guitball_01", "gp_fx_guitlast_streaks_01", "gp_fx_houseband_encore_circlefire01", "gp_fx_houseband_encore_circlefire02", "gp_fx_houseband_encore_circlefire03", "gp_fx_houseband_encore_upfire01", "gp_fx_houseband_encore_upfire02", "gp_fx_hw_hita_bicepenergy01", "gp_fx_hw_hita_bicepenergy02", "gp_fx_hw_hita_bodyenergy01", "gp_fx_hw_hita_forearmenergy01", "gp_fx_hw_hita_forearmenergy02", "gp_fx_hw_hita_headenergy01", "gp_fx_hw_hita_shinenergy01", "gp_fx_hw_hita_shinenergy02", "gp_fx_hw_hita_shoulderglow01", "gp_fx_hw_hita_shouldersmoke01", "gp_fx_hw_hita_shouldersparks01", "gp_fx_hw_hita_splashcenter01", "gp_fx_hw_hita_splashrings01", "gp_fx_hw_hita_splashsparks01", "gp_fx_hw_hita_thighenergy01", "gp_fx_hw_hita_thighenergy02", "gp_fx_hw_hita_thighglow01", "gp_fx_hw_hita_thighsmoke01", "gp_fx_hw_hita_thighsparks01", "gp_fx_hw_hita_towerenergy01", "gp_fx_hw_hita_towerenergy02", "gp_fx_hw_hita_towersparks01", "gp_fx_hw_hita_towersparks02", "gp_fx_hw_shota_bodyenergy01", "gp_fx_hw_shota_energyball01", "gp_fx_hw_shota_energytrail01", "gp_fx_hw_shota_muzzleflash01", "gp_fx_hw_shota_muzzlerings01", "gp_fx_hw_shota_muzzlesparks01", "gp_fx_hw_shotb_bodyenergy01", "gp_fx_hw_shotc_bodyenergy01", "gp_fx_hw_shotc_energyball01", "gp_fx_hw_shotc_energytrail01", "gp_fx_hw_shotc_muzzlerings01", "gp_fx_intro01_shakedust01", "gp_fx_intro03_ax_fire01", "gp_fx_intro03_ax_glow01", "gp_fx_intro03_ax_glow02", "gp_fx_intro03_ax_sparkles01", "gp_fx_intro03_ax_sparks01", "gp_fx_intro03_ax_wisps01", "gp_fx_intro03_dg_armdust01", "gp_fx_intro03_dg_footdust01", "gp_fx_intro03_lightning01", "gp_fx_intro03_lightningglow01", "gp_fx_intro03_statue_beambits01", "gp_fx_intro03_statue_beamdust01", "gp_fx_line01_attachelectric_01", "gp_fx_line01_attachflare_01", "gp_fx_line01_attachsmoke_01", "gp_fx_line01_attachsparkribbons_01", "gp_fx_mouthfog_01", "gp_fx_ms_candlefire_01", "gp_fx_ms_fireplacefire_01", "gp_fx_ms_pyrofire_01", "gp_fx_newsmoke_flash_01", "gp_fx_newsmoke_topsmokefire_01", "gp_fx_newsparks_bottomflare_01", "gp_fx_newsparks_fastsparks_01", "gp_fx_newsparks_smokecolumn_01", "gp_fx_newsparks_smokecolumn_02", "gp_fx_newsparks_sparks_01", "gp_fx_nm_glow_01", "gp_fx_nm_smoke_01", "gp_fx_outro02_backsmoke01", "gp_fx_outro02_fingerelectric01", "gp_fx_pitsmoke_01", "gp_fx_pitsmoke_02", "gp_fx_poolfire_01", "gp_fx_poolfire_02", "gp_fx_poolglows_01", "gp_fx_punkstage_encore_bodysmoke01", "gp_fx_punkstage_encore_poof01", "gp_fx_punkstage_encore_poof02", "gp_fx_pyro_baseflashes_01", "gp_fx_sarc_linelight_01", "gp_fx_sarc_linelight_02", "gp_fx_scifi_elecexplo_glow_02", "gp_fx_scifi_elecexplo_smoke_01", "gp_fx_scifi_elecexplo_sparks_01", "gp_fx_sd_ampzilla_electric01", "gp_fx_sd_ampzilla_glow01", "gp_fx_sd_ampzilla_smoke01", "gp_fx_sd_ampzilla_sparks01", "gp_fx_sd_dg_attacka_glow01", "gp_fx_sd_dg_attacka_sparks01", "gp_fx_sd_dg_attackf_bodyelectric01", "gp_fx_sd_dg_attackf_bodysparks01", "gp_fx_sd_dg_attackf_electric01", "gp_fx_sd_dg_attackf_glow01", "gp_fx_sd_dg_attackf_sparks01", "gp_fx_sd_dg_attackf_sparks02", "gp_fx_sd_dg_hita_rings01", "gp_fx_sd_dg_hita_sparks01", "gp_fx_sd_dg_slidedust01", "gp_fx_singledrips_01", "gp_fx_singledrips_02", "gp_fx_smallfireplume_01", "gp_fx_sparkfountain_big_sparks_01", "gp_fx_sparkfountain_frontflash_01", "gp_fx_sparkfountain_frontglow_01", "gp_fx_sparkfountain_glowsmoke_01", "gp_fx_sparkfountain_sparks_01", "gp_fx_sparkshowersmall_lensflares_01", "gp_fx_sparkshowersmall_multisparks_01", "gp_fx_sparkshowersmall_smokedown_01", "gp_fx_sparkshowersmall_smokeup_01", "gp_fx_sparkshowersmall_sparks_01", "gp_fx_sparkshowersmall_sparksup_01", "gp_fx_sparkshowersmall_topglows_01", "gp_fx_sparx_01", "gp_fx_spinnersparks_01", "gp_fx_spit_small_01", "gp_fx_stars_01", "gp_fx_tesla_flashes_01", "gp_fx_tesla_smallglow_01", "gp_fx_tesla_sparks_01", "gp_fx_topsparks_01", "gp_fx_topsparks_flashes_02", "gp_fx_topsparks_radiate_01", "gp_fx_topsparks_radiate_02", "gp_fx_topsparks_small_flashes_01", "gp_fx_topsparks_small_randsparks_01", "gp_fx_topsparks_small_smoke_01", "gp_fx_topsparks_small_sparks_01", "gp_fx_topsparks_waterfall_01", "gp_fx_topsparks_waterfallshort_01", "gp_fx_wallglow_01", "gp_fx_wallglow_02", "gp_fx_wallglow_04", "gp_fx_wallsmoke_01", "gp_fx_wallsmoke_02", "gp_fx_wallsmoke_03", "gp_fx_waterfall_rings_01", "gp_fx_waterfallsplashesbottom_bits_01", "gp_fx_waterfallsplashesbottom_smoke_01", "gp_fx_widesparks_01", "gp_fx_widesparks_glows_01", "gp_fx_widesparks_glowslens_01", "gp_godray_dust_02", "gp_p_momentsmoke_01", "gp_particlefiretest", "gp_starpower_teslasparks01", "gp_temple_ambient_dust_01", "gp_temple_ambient_dust_02"];
// Carcasas de foco (models/lighthousings, global.pak): indice = el que entiende el hook
export const HOUSINGS = ['Foco con destello', 'Solo haz con destello', 'Foco clasico', 'Foco de granero', 'Foco de cuenco', 'Destello de momento', 'Foco pequeno', 'Solo haz'];
// Modelos del juego siempre cargados (global.pak). Ruta relativa a data/models (el hook cambia / por /).
export const GAME_MODELS = [
  { path: 'GH_Rockers/Silhouette_M.skin', label: 'Silueta de rockero' },
  { path: 'Real_Crowd/crowd_handl_rock.skin', label: 'Mano del publico: cuernos (izq.)' },
  { path: 'Real_Crowd/crowd_handr_rock.skin', label: 'Mano del publico: cuernos (der.)' },
  { path: 'Real_Crowd/crowd_handl_fist.skin', label: 'Mano del publico: puno (izq.)' },
  { path: 'Real_Crowd/crowd_handr_fist.skin', label: 'Mano del publico: puno (der.)' },
  { path: 'Real_Crowd/crowd_handl_clap.skin', label: 'Mano del publico: aplauso (izq.)' },
  { path: 'Real_Crowd/crowd_handr_clap.skin', label: 'Mano del publico: aplauso (der.)' },
  { path: 'LightHousings/GO_LH_Bowl/GO_LH_Bowl.mdl', label: 'Foco de cuenco (apagado)' },
  { path: 'LightHousings/GO_BarnHousing01/GO_BarnHousing01.mdl', label: 'Foco de granero (apagado)' },
];
