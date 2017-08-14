#include "stdafx.h"
#include <windows.h>
#define OUTSIDE_SPEEX
#include "CLibretro.h"
#include "libretro.h"
#include "io/gl_render.h"
#include "gui/utf8conv.h"

using namespace std;
using namespace utf8util;

#define SAMPLE_COUNT 8192

	bool Fifo::init(size_t size)
	{
		_mutex = SDL_CreateMutex();

		if (!_mutex)
		{
			return false;
		}

		_buffer = (uint8_t*)malloc(size);

		if (_buffer == NULL)
		{
			SDL_DestroyMutex(_mutex);
			return false;
		}

		_size = _avail = size;
		_first = _last = 0;
		return true;
	}
	void Fifo::destroy()
	{
		::free(_buffer);
		SDL_DestroyMutex(_mutex);
	}
	void Fifo::reset()
	{
		_avail = _size;
		_first = _last = 0;
	}


	void Fifo::read(void* data, size_t size)
	{
		SDL_LockMutex(_mutex);

		size_t first = size;
		size_t second = 0;

		if (first > _size - _first)
		{
			first = _size - _first;
			second = size - first;
		}

		uint8_t* src = _buffer + _first;
		memcpy(data, src, first);
		memcpy((uint8_t*)data + first, _buffer, second);

		_first = (_first + size) % _size;
		_avail += size;

		SDL_UnlockMutex(_mutex);
	}
	void Fifo::write(const void* data, size_t size)
	{
		SDL_LockMutex(_mutex);

		size_t first = size;
		size_t second = 0;

		if (first > _size - _last)
		{
			first = _size - _last;
			second = size - first;
		}

		uint8_t* dest = _buffer + _last;
		memcpy(dest, data, first);
		memcpy(_buffer, (uint8_t*)data + first, second);

		_last = (_last + size) % _size;
		_avail -= size;

		SDL_UnlockMutex(_mutex);
	}

	size_t Fifo::occupied()
	{
		size_t avail;

		SDL_LockMutex(_mutex);
		avail = _size - _avail;
		SDL_UnlockMutex(_mutex);

		return avail;
	}

	size_t Fifo::free()
	{
		size_t avail;

		SDL_LockMutex(_mutex);
		avail = _avail;
		SDL_UnlockMutex(_mutex);

		return avail;
	}

	static void sdl_audio_callback(void* user_data, Uint8* out, int count) {
		((Audio*)user_data)->fill_buffer(out, count);
	}

	bool Audio::init(unsigned sample_rate)
	{
		_opened = true;
		_mute = false;
		_scale = 1.0f;
		_samples = NULL;

		_coreRate = 0;
		_resampler = NULL;
		_sampleRate = 44100;
		setRate(sample_rate);

		SDL_AudioSpec want;
		memset(&want, 0, sizeof(want));

		want.freq = 44100;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = 1024;
		want.callback = ::sdl_audio_callback;
		want.userdata = this;


		// Initialize the audio system
		if (SDL_AudioInit(NULL) < 0)return false;
		if (SDL_OpenAudio(&want, &_audioSpec) < 0)
		{
			fprintf(stderr, "Could not open the audio hardware or the desired audio output format\n");
			return 1;
		}
		_fifo = new Fifo();
		_fifo->init(_audioSpec.size * 2);
	
		SDL_PauseAudio(0);
	}
	void Audio::destroy()
	{
		{
			if (_resampler != NULL)
			{
				speex_resampler_destroy(_resampler);
			}
		}
	}
	void Audio::reset()
	{

	}

	bool Audio::setRate(double rate)
	{
		if (_resampler != NULL)
		{
			speex_resampler_destroy(_resampler);
			_resampler = NULL;
		}

		_coreRate = floor(rate + 0.5);

		if (_coreRate != _sampleRate)
		{
			int error;
			_resampler = speex_resampler_init(2, _coreRate, _sampleRate, SPEEX_RESAMPLER_QUALITY_DEFAULT, &error);
		}
		return true;

	}
	void Audio::mix(const int16_t* samples, size_t frames)
	{
		_samples = samples;
		_frames = frames;

		uint32_t in_len = frames * 2;
		uint32_t out_len = trunc((uint32_t)(in_len * _sampleRate / _coreRate))+1;

		int16_t* output = (int16_t*)alloca(out_len * 2);

		if (output == NULL)
		{
			return;
		}

		if (_resampler != NULL)
		{
			int error = speex_resampler_process_int(_resampler, 0, samples, &in_len, output, &out_len);
		}
		else
		{
			memcpy(output, samples, out_len * 2);
		}

		size_t size = out_len * 2;

	again:
		size_t avail = _fifo->free();

		if (size > avail)
		{
			SDL_Delay(1);
			goto again;
		}

		_fifo->write(output, size);
	}

	void Audio::fill_buffer(Uint8* out, int count) {
		size_t avail = _fifo->occupied();
		if (avail < (size_t)count)
		{
			_fifo->read((void*)out, avail);
			memset((void*)(out + avail), 0, count - avail);
		}
		else
		{
			_fifo->read((void*)out, count);
		}
	}



static struct {
	HMODULE handle;
	bool initialized;

	void(*retro_init)(void);
	void(*retro_deinit)(void);
	unsigned(*retro_api_version)(void);
	void(*retro_get_system_info)(struct retro_system_info *info);
	void(*retro_get_system_av_info)(struct retro_system_av_info *info);
	void(*retro_set_controller_port_device)(unsigned port, unsigned device);
	void(*retro_reset)(void);
	void(*retro_run)(void);
	//	size_t retro_serialize_size(void);
	//	bool retro_serialize(void *data, size_t size);
	//	bool retro_unserialize(const void *data, size_t size);
	//	void retro_cheat_reset(void);
	//	void retro_cheat_set(unsigned index, bool enabled, const char *code);
	bool(*retro_load_game)(const struct retro_game_info *game);
	//	bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
	void(*retro_unload_game)(void);
	//	unsigned retro_get_region(void);
	//	void *retro_get_memory_data(unsigned id);
	//	size_t retro_get_memory_size(unsigned id);
} g_retro;

static void core_unload() {
	if (g_retro.initialized)
	g_retro.retro_deinit();
	if (g_retro.handle)
	{
		FreeLibrary(g_retro.handle);
		g_retro.handle = NULL;
	}
	
}

static void core_log(enum retro_log_level level, const char *fmt, ...) {
	char buffer[4096] = { 0 };
	char buffer2[4096] = { 0 };
	static const char * levelstr[] = { "dbg", "inf", "wrn", "err" };
	va_list va;

	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	if (level == 0)
		return;

	sprintf(buffer2, "[%s] %s", levelstr[level], buffer);
	wstring wtf = utf16_from_utf8(buffer2);
	OutputDebugString(wtf.c_str());
	fprintf(stdout, "%s",buffer2);
	if (level == RETRO_LOG_ERROR)
		exit(EXIT_FAILURE);
}


uintptr_t core_get_current_framebuffer() {
	return g_video.fbo_id;
}

#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

void GetThisPath(TCHAR* dest, size_t destSize)
{
	TCHAR Buffer[512];
	DWORD dwRet;
	dwRet = GetCurrentDirectory(512, dest);
}

bool core_environment(unsigned cmd, void *data) {
	bool *bval;
	char *sys_path = "system";

	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback *cb = (struct retro_log_callback *)data;
		cb->log = core_log;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		bval = (bool*)data;
		*bval = true;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // 9
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: // 31
	{
		char **ppDir = (char**)data;
		*ppDir = (char*)sys_path;
		return true;
	}
	break;

	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		{
			struct retro_variable * variable = (struct retro_variable*)data;
			if (!strncmp(variable->key, "mupen64plus-aspect", strlen("mupen64plus-aspect")))
			{
				variable->value = "4:3";
			}
			if (!strncmp(variable->key, "mupen64plus-43screensize", strlen("mupen64plus-43screensize")))
			{
					variable->value = "640x480";
			}
		}
	}
	break;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		const enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;
		if (*fmt > RETRO_PIXEL_FORMAT_RGB565)
			return false;
		return video_set_pixel_format(*fmt);
	}
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback *hw = (struct retro_hw_render_callback*)data;
		hw->get_current_framebuffer = core_get_current_framebuffer;
		hw->get_proc_address = (retro_hw_get_proc_address_t)wglGetProcAddress;
		g_video.hw = *hw;
		return true;
	}
	default:
		core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
		return false;
	}

	return false;
}

static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	video_refresh(data, width, height, pitch);
}


static void core_input_poll(void) {
	
}

static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	if (port || index || device != RETRO_DEVICE_JOYPAD)
		return 0;

	return 0;
}


void CLibretro::core_audio_sample(int16_t left, int16_t right)
{
	if (_samplesCount < SAMPLE_COUNT - 1)
	{
		_samples[_samplesCount++] = left;
		_samples[_samplesCount++] = right;
	}
}


size_t CLibretro::core_audio_sample_batch(const int16_t *data, size_t frames)
{
	if (_samplesCount < SAMPLE_COUNT - frames * 2 + 1)
	{
		memcpy(_samples + _samplesCount, data, frames * 2 * sizeof(int16_t));
		_samplesCount += frames * 2;
	}
	return frames;
}


static void core_audio_sample(int16_t left, int16_t right) {
	CLibretro* lib = CLibretro::GetSingleton();
	lib->core_audio_sample(left, right);
}


static size_t core_audio_sample_batch(const int16_t *data, size_t frames) {
	CLibretro* lib = CLibretro::GetSingleton();
	lib->core_audio_sample_batch(data, frames);
	return frames;
}

bool core_load(TCHAR *sofile) {
	void(*set_environment)(retro_environment_t) = NULL;
	void(*set_video_refresh)(retro_video_refresh_t) = NULL;
	void(*set_input_poll)(retro_input_poll_t) = NULL;
	void(*set_input_state)(retro_input_state_t) = NULL;
	void(*set_audio_sample)(retro_audio_sample_t) = NULL;
	void(*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
	memset(&g_retro, 0, sizeof(g_retro));
	g_retro.handle = LoadLibrary(sofile);

#define die() do { FreeLibrary(g_retro.handle); return false; } while(0)
#define libload(name) GetProcAddress(g_retro.handle, name)
#define load(name) if (!(*(void**)(&g_retro.#name)=(void*)libload(#name))) die()
#define load_sym(V,name) if (!(*(void**)(&V)=(void*)libload(#name))) die()
#define load_retro_sym(S) load_sym(g_retro.S, S)
	load_retro_sym(retro_init);
	load_retro_sym(retro_deinit);
	load_retro_sym(retro_api_version);
	load_retro_sym(retro_get_system_info);
	load_retro_sym(retro_get_system_av_info);
	load_retro_sym(retro_set_controller_port_device);
	load_retro_sym(retro_reset);
	load_retro_sym(retro_run);
	load_retro_sym(retro_load_game);
	load_retro_sym(retro_unload_game);

	load_sym(set_environment, retro_set_environment);
	load_sym(set_video_refresh, retro_set_video_refresh);
	load_sym(set_input_poll, retro_set_input_poll);
	load_sym(set_input_state, retro_set_input_state);
	load_sym(set_audio_sample, retro_set_audio_sample);
	load_sym(set_audio_sample_batch, retro_set_audio_sample_batch);
	//set libretro func pointers
	set_environment(core_environment);
	set_video_refresh(core_video_refresh);
	set_input_poll(core_input_poll);
	set_input_state(core_input_state);
	set_audio_sample(core_audio_sample);
	set_audio_sample_batch(core_audio_sample_batch);

	g_retro.retro_init();
	g_retro.initialized = true;
}

static void noop() { int i = 20;
i++;
}



CLibretro* CLibretro::m_Instance = 0 ;
CLibretro* CLibretro::CreateInstance(HWND hwnd )
{
	if (0 == m_Instance)
	{
		m_Instance = new CLibretro( ) ;
		m_Instance->init( hwnd) ;
	}
	return m_Instance ;
}

CLibretro* CLibretro::GetSingleton( )
{
	return m_Instance ;
}
//////////////////////////////////////////////////////////////////////////////////////////

bool CLibretro::running()
{
	return isEmulating;
}

CLibretro::CLibretro()
{
	isEmulating = false;
}

CLibretro::~CLibretro(void)
{
	kill();
}


long long milliseconds_now() {
	static LARGE_INTEGER s_frequency;
	static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
	if (s_use_qpc) {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (1000LL * now.QuadPart) / s_frequency.QuadPart;
	}
	else {
		return GetTickCount();
	}
}

bool CLibretro::loadfile(char* filename)
{
	retro_system_av_info av = { 0 };
	struct retro_system_info system = {0};
	struct retro_game_info info = { filename, 0 };

	g_video = { 0 };
//	g_video.hw.version_major = 3;
//	g_video.hw.version_minor = 3;
//	g_video.hw.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
	g_video.hw.context_reset = ::noop;
	g_video.hw.context_destroy = ::noop;

	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen("CON", "w", stdout);
	core_load(L"mupen64plus_libretro.dll");

	FILE *Input = fopen(filename, "rb");
	if (!Input) return(NULL);
	// Get the filesize
	fseek(Input, 0, SEEK_END);
	int Size = ftell(Input);
	fseek(Input, 0, SEEK_SET);
	BYTE *Memory = (BYTE *)malloc(Size);
	if (!Memory) return(NULL);
	if (fread(Memory, 1, Size, Input) != (size_t)Size) return(NULL);
	if (Input) fclose(Input);
	Input = NULL;

	info.path = filename;
	info.data = NULL;
	info.size = Size;
	info.meta = NULL;

	g_retro.retro_get_system_info(&system);
	if (!system.need_fullpath) {
		info.data = malloc(info.size);
		memcpy((BYTE*)info.data, Memory, info.size);
		free(Memory);
	}

	if (!g_retro.retro_load_game(&info)) return false;
	g_retro.retro_get_system_av_info(&av);
	::video_configure(&av.geometry,emulator_hwnd);
	_samples = (int16_t*)malloc(SAMPLE_COUNT);
	_audio = new Audio();
	double orig_ratio = (double)60 /av.timing.fps;
	double sampleRate = av.timing.sample_rate * 60 / av.timing.fps;
	_audio->init(sampleRate);
	

	//WindowsAudio::Open(2, av.timing.sample_rate);


	if (info.data)
	free((void*)info.data);


	framecount = 0;
	rateticks = 1000 / av.timing.fps;
	baseticks = milliseconds_now();
	baseticks_base = baseticks;
	lastticks = baseticks;
	rate = av.timing.fps;
	isEmulating = true;
	listDeltaMA.clear();
	target_fps = av.timing.fps;
	target_samplerate = 44100;
	return isEmulating;
}

void CLibretro::splash()
{
	if (!isEmulating){
		PAINTSTRUCT ps;
		HDC pDC = BeginPaint(emulator_hwnd,&ps);
		RECT rect;
		GetClientRect(emulator_hwnd, &rect);
		HBRUSH hBrush = (HBRUSH)::GetStockObject(BLACK_BRUSH);
		::FillRect(pDC, &rect, hBrush);
		EndPaint(emulator_hwnd, &ps);
	}
}

void CLibretro::run()
{
	if(isEmulating)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
		_samplesCount = 0;
		do
		{
			g_retro.retro_run();
		} while (_samplesCount == 0);

		_audio->mix(_samples, _samplesCount / 2);
		//Sleep(1);
	}
}
bool CLibretro::init(HWND hwnd)
{
	isEmulating = false;
	emulator_hwnd = hwnd;
	return true;
}

void CLibretro::kill()
{
	isEmulating = false;
	//WindowsAudio::Close();
}
