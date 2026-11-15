import re
import subprocess
import os

with open('tools/engine_params_decompiled.txt', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Framerate & VSync (Ultra-smooth PC display)
text = text.replace('StructInt target_ntsc_framerate = 60', 'StructInt target_ntsc_framerate = 240')
text = text.replace('StructInt gpu_vsync_time_interval = 30', 'StructInt gpu_vsync_time_interval = 0')

# 2. Draw Distance & Culling (LOD Pop-In Removal)
text = text.replace('StructInt default_far_plane_distance = 300', 'StructInt default_far_plane_distance = 1500')
text = text.replace('StructInt cull_max_instances = 400', 'StructInt cull_max_instances = 1500')
text = text.replace('StructInt cull_max_geom = 1000', 'StructInt cull_max_geom = 3500')
text = text.replace('StructInt cull_max_occluder = 256', 'StructInt cull_max_occluder = 512')
text = text.replace('StructFloat cull_smallobj = 0.000029999999242136255', 'StructFloat cull_smallobj = 0.00000100000000000000')
text = text.replace('Floats [600.000, 1000.00]', 'Floats [1800.00, 3500.00]')
text = text.replace('Floats [200.000, 500.000]', 'Floats [600.000, 1500.00]')

# 3. Audio & Streams (Safe 2x expansion within guest memory budget)
text = text.replace('StructInt max_streams = 15', 'StructInt max_streams = 32')
text = text.replace('StructInt max_sound_entries = 1536', 'StructInt max_sound_entries = 3072')
text = text.replace('StructInt num_sound_instances = 64', 'StructInt num_sound_instances = 128')
text = text.replace('StructInt pool_text_instances = 1024', 'StructInt pool_text_instances = 2048')
text = text.replace('StructInt fmod_memory_size = 12582912', 'StructInt fmod_memory_size = 25165824')

# 4. Engine Buffers & Character Skinning (Aligned to PC Windx standards)
text = text.replace('StructInt cpu_skinning_buffer_size = 17500', 'StructInt cpu_skinning_buffer_size = 40000')
text = text.replace('StructInt cpu_skin_wrap_buffer_size = 7500', 'StructInt cpu_skin_wrap_buffer_size = 15000')
text = text.replace('StructInt node_instances_buff = 312', 'StructInt node_instances_buff = 600')
text = text.replace('StructInt node_statics_buff = 240', 'StructInt node_statics_buff = 500')
text = text.replace('StructInt node_sort_buff = 256', 'StructInt node_sort_buff = 512')
text = text.replace('StructInt node_misc_buff = 2600', 'StructInt node_misc_buff = 4000')
text = text.replace('StructInt node_2d_buff = 330', 'StructInt node_2d_buff = 600')
text = text.replace('StructInt node_particle_buff = 1400', 'StructInt node_particle_buff = 2500')
text = text.replace('StructInt node_particle_node = 256', 'StructInt node_particle_node = 512')
text = text.replace('StructInt node_light_buff = 150', 'StructInt node_light_buff = 300')

with open('tools/engine_params_upgraded.txt', 'w', encoding='utf-8') as f:
    f.write(text)

print('Updated tools/engine_params_upgraded.txt successfully!')


