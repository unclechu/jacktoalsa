/**
 * Metachronica JACK to ALSA utility
 * Forwarding JACK to ALSA.
 * Start JACK with dummy device and send sound to ALSA via libasound.
 *
 * License: GPLv3
 *
 * TODO:
 *   ignoring overruns
 *   32 and 24 bit depth
 *   watch for system:playback and system:capture and forward to alsa
 *
 * FIXME:
 *   dropouts when no playback ports
 *   set hardware params for custom alsa card
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <jack/jack.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <unistd.h>

// default: stereo
short num_capture_channels = 2; // jack-outputs, alsa-inputs
short num_playback_channels = 2; // jack-inputs, alsa-outputs

// jack
jack_client_t *jack_client = NULL;
char jack_client_name[128] = "meta_jacktoalsa";
uint32_t buffer_size = 0;
uint32_t sample_rate = 0;
jack_port_t **capture_ports = NULL;
jack_port_t **playback_ports = NULL;
jack_default_audio_sample_t **capture_buf = NULL;
jack_default_audio_sample_t **playback_buf = NULL;

// alsa
snd_pcm_t *alsa_playback_handle = NULL;
snd_pcm_t *alsa_capture_handle = NULL;
char alsa_card_playback[128] = "default";
char alsa_card_capture[128] = "default";
snd_pcm_format_t alsa_bit_depth = SND_PCM_FORMAT_S16;
int16_t *playback_buf16bit = NULL;
int16_t *capture_buf16bit = NULL;

inline int16_t float_to_int16(float v) {
    if (v >= 1.0)
        return 32767;
    else if (v <= -1.0)
        return -32768;
    else
        return floorf(v * 32768.0);
}

inline float int16_to_float(int16_t v) {
    return v / 32768.0;
}

int jack_process(jack_nframes_t nframes, void *arg) {
    int res;
    int channel, bufval, n;

    // get from jack-input and write to alsa-playback
    if (num_playback_channels > 0) {
        for (channel=0; channel<num_playback_channels; channel++) {
            playback_buf[channel] = (jack_default_audio_sample_t *)
                jack_port_get_buffer(playback_ports[channel], nframes);
        }

        if (alsa_bit_depth == SND_PCM_FORMAT_S16) {
            for ( bufval = 0, n = 0;
                  bufval < (nframes * num_playback_channels);
                  bufval = (bufval + num_playback_channels), n++ )
                for (channel=0; channel<num_playback_channels; channel++)
                    playback_buf16bit[bufval + channel] = float_to_int16(playback_buf[channel][n]);
        }

        res = snd_pcm_writei(alsa_playback_handle, playback_buf16bit, nframes);
        if (res == -EPIPE) { // heal the overruns
            res = snd_pcm_recover(alsa_playback_handle, res, 1);
            if (res >= 0)
                res = snd_pcm_writei(alsa_playback_handle, playback_buf16bit, nframes);
        }
    }

    // get from alsa-capture and write to jack-output
    if (num_capture_channels > 0) {
        for (channel=0; channel<num_capture_channels; channel++) {
            capture_buf[channel] = (jack_default_audio_sample_t *)
                jack_port_get_buffer(capture_ports[channel], nframes);
        }

        res = snd_pcm_readi(alsa_capture_handle, capture_buf16bit, nframes);
        if (res == -EPIPE) snd_pcm_prepare(alsa_capture_handle); // heal the overruns

        if (alsa_bit_depth == SND_PCM_FORMAT_S16) {
            for ( bufval = 0, n = 0;
                  bufval < (nframes * num_capture_channels);
                  bufval = (bufval + num_capture_channels), n++ )
                for (channel=0; channel<num_capture_channels; channel++)
                    capture_buf[channel][n] = int16_to_float(capture_buf16bit[bufval + channel]);
        }
    }

    return 0;
}

int jack_new_buffer(jack_nframes_t nframes, void *arg) {
    buffer_size = nframes;
    fprintf(stdout, "JACK: new buffer size: %d\n", buffer_size);

    fprintf(stdout, "ALSA: reallocate memory for buffer\n");
    if (alsa_bit_depth == SND_PCM_FORMAT_S16) {
        if (num_playback_channels > 0) {
            if (playback_buf16bit != NULL) free(playback_buf16bit);
            playback_buf16bit = malloc( (nframes * sizeof(short)) * num_playback_channels );
        }

        if (num_capture_channels > 0) {
            if (capture_buf16bit != NULL) free(capture_buf16bit);
            capture_buf16bit = malloc( (nframes * sizeof(short)) * num_capture_channels );
        }
    } else {
        fprintf(stderr, "Unsupported bit depth of ALSA (in jack_new_buffer)\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

int jack_sample_rate_plug(uint32_t sample_rate, void *arg) {
    fprintf(stderr, "JACK: changing of sample rate is unsupported\n");
}

int init_jack() {
    fprintf(stdout, "JACK initialization...\n");

    int i;
    char port_name[128];

    jack_status_t status;

    fprintf(stdout, "JACK: opening client\n");
    jack_client = jack_client_open( jack_client_name,
                                    JackNullOption,
                                    &status,
                                    NULL );

    if (jack_client == NULL) {
        fprintf(stderr, "JACK: client open error\n");
        return 1;
    }

    if (status & JackNameNotUnique) {
        fprintf(stderr, "JACK: client name already taken"
                        " (%s already started?)\n", jack_client_name);
        return 1;
    }

    if (num_playback_channels > 0) {
        fprintf(stdout, "JACK: registering input (playback) ports\n");
        playback_ports = malloc(sizeof(jack_port_t*) * num_playback_channels);
        playback_buf = malloc(sizeof(jack_default_audio_sample_t*) * num_playback_channels);
        for (i=0; i<num_playback_channels; i++) {
            sprintf(port_name, "playback_%d", i+1);
            playback_ports[i] = jack_port_register( jack_client, 
                                                    port_name,
                                                    JACK_DEFAULT_AUDIO_TYPE,
                                                    JackPortIsInput | JackPortIsPhysical,
                                                    0 );
            if (playback_ports[i] != NULL) {
                fprintf(stdout, "JACK: input (playback) port \"%s\" registered\n", port_name);
            } else {
                fprintf(stderr, "JACK: no more ports available\n");
                return 1;
            }
        }
    }

    if (num_capture_channels > 0) {
        fprintf(stdout, "JACK: registering output (capture) ports\n");
        capture_ports = malloc(sizeof(jack_port_t*) * num_capture_channels);
        capture_buf = malloc(sizeof(jack_default_audio_sample_t*) * num_capture_channels);
        for (i=0; i<num_capture_channels; i++) {
            sprintf(port_name, "capture_%d", i+1);
            capture_ports[i] = jack_port_register( jack_client, 
                                                   port_name,
                                                   JACK_DEFAULT_AUDIO_TYPE,
                                                   JackPortIsOutput | JackPortIsPhysical,
                                                   0 );
            if (capture_ports[i] != NULL) {
                fprintf(stdout, "JACK: output (capture) port \"%s\" registered\n", port_name);
            } else {
                fprintf(stderr, "JACK: no more ports available\n");
                return 1;
            }
        }
    }

    fprintf(stdout, "JACK: binding process callback\n");
    jack_set_process_callback(jack_client, jack_process, 0);

    fprintf(stdout, "JACK: binding sample rate change callback\n");
    jack_set_sample_rate_callback(jack_client, jack_sample_rate_plug, 0);

    fprintf(stdout, "JACK: bind callback to set buffer size\n");
    jack_set_buffer_size_callback(jack_client, jack_new_buffer, 0);

    fprintf(stdout, "JACK: getting sample rate\n");
    sample_rate = jack_get_sample_rate(jack_client);

    fprintf(stdout, "JACK: getting buffer size\n");
    jack_new_buffer(jack_get_buffer_size(jack_client), 0);

    // initialize alsa
    if (init_alsa() != 0) {
        fprintf(stderr, "Cannot initialize ALSA\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "JACK: activating client\n");
    if (jack_activate(jack_client)) {
        fprintf(stderr, "JACK: activating client error\n");
        return 1;
    }

    fprintf(stdout, "JACK is initialized\n");
    return 0;
}

int init_alsa() {
    fprintf(stdout, "ALSA initialization...\n");

    int res;

    // playback
    if (num_playback_channels > 0) {
        fprintf(stdout, "ALSA: opening pcm playback\n");
        res = snd_pcm_open(&alsa_playback_handle, alsa_card_playback, SND_PCM_STREAM_PLAYBACK, 0);
        if (res < 0)
            fprintf(stderr, "ALSA: cannot open pcm playback \"%s\": %s\n",
                             alsa_card_playback, snd_strerror(res) );

        fprintf(stdout, "ALSA: set playback parameters\n");
        res = snd_pcm_set_params( alsa_playback_handle, alsa_bit_depth,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  num_playback_channels, sample_rate, 1, 0);
        if (res < 0)
            fprintf(stderr, "ALSA: cannot set playback parameters: %s\n", snd_strerror(res));
    }

    // capture
    if (num_capture_channels > 0) {
        fprintf(stdout, "ALSA: opening pcm capture\n");
        res = snd_pcm_open(&alsa_capture_handle, alsa_card_capture, SND_PCM_STREAM_CAPTURE, 0);
        if (res < 0)
            fprintf(stderr, "ALSA: cannot open pcm capture \"%s\": %s\n",
                             alsa_card_capture, snd_strerror(res) );

        fprintf(stdout, "ALSA: set capture parameters\n");
        res = snd_pcm_set_params( alsa_capture_handle, alsa_bit_depth,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  num_capture_channels, sample_rate, 1, 0);
        if (res < 0)
            fprintf(stderr, "ALSA: cannot set capture parameters: %s\n", snd_strerror(res));
    }

    fprintf(stdout, "ALSA is initialized\n");
    return 0;
}

char alsa_card_pfx[] = "--alsa-card=";
char alsa_card_playback_pfx[] = "--alsa-card-playback=";
char alsa_card_capture_pfx[] = "--alsa-card-capture=";
char jack_client_name_pfx[] = "--jack-client=";
char ports_num_pfx[] = "--ports-num=";
char playback_ports_pfx[] = "--playback-ports=";
char capture_ports_pfx[] = "--capture-ports=";
char usage[] = "\n"
"USAGE\n"
"=====\n"
"\n"
"-h, --help\n"
"    Show this usage information.\n"
"\n"
"--alsa-card=NAME, --alsa-card-playback=NAME, --alsa-card-capture=NAME\n"
"    Set specific ALSA card name. Also you can use ALSA_CARD environment\n"
"    variable.\n\n"
"    Default value: \"default\"\n\n"
"    Examples:\n"
"        --alsa-card=default\n"
"        --alsa-card-playback=hw:0\n"
"        --alsa-card-capture=hw:USB\n"
"\n"
"--jack-client=NAME\n"
"    Set specific JACK client name.\n\n"
"    Default value: \"meta_jacktoalsa\"\n\n"
"    Examples:\n"
"        --jack-client=meta_jacktoalsa\n"
"        --jack-client=alsa\n"
"\n"
"--ports-num=NUM, --playback-ports=NUM, --capture-ports=NUM\n"
"    Set specific number of playback and capture ports.\n\n"
"    Default value: \"2\" (stereo)\n\n"
"    Examples:\n"
"        --ports-num=1 (mono)\n"
"        --playback-ports=2 (stereo)\n"
"        --capture-ports=6 (5.1)\n"
"\n";

bool catch_arg(char *target, char *prefix, char *arg) {
    int i, n;
    if (strlen(arg) < strlen(prefix)) return false;

    for (i=0; i<strlen(prefix); i++) {
        if (arg[i] != prefix[i]) return false;
    }

    for (i=0, n=strlen(prefix); ; i++, n++) {
        if (i >= sizeof(target)-1) {
            target[i] = '\0';
        }
        target[i] = arg[n];
        if (arg[n] == '\0') break;
    }

    return true;
}

int main(int argc, char **argv) {
    // parsing arguments
    int i;
    char argval[128];
    if (argc > 1) {
        for (i=1; i<argc; i++) {
            // usage info
            if (strcmp("--help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0) {
                fprintf(stdout, "%s", usage);
                return EXIT_SUCCESS;

            // ALSA card
            } else if (catch_arg(argval, alsa_card_pfx, argv[i])) {
                strcpy(alsa_card_playback, argval);
                strcpy(alsa_card_capture, argval);
                fprintf(stdout, "Custom ALSA card from arguments: \"%s\"\n", argval);
            } else if (catch_arg(argval, alsa_card_playback_pfx, argv[i])) {
                strcpy(alsa_card_playback, argval);
                fprintf(stdout, "Custom ALSA playback card from arguments: \"%s\"\n", argval);
            } else if (catch_arg(argval, alsa_card_capture_pfx, argv[i])) {
                strcpy(alsa_card_capture, argval);
                fprintf(stdout, "Custom ALSA capture card from arguments: \"%s\"\n", argval);

            // JACK client name
            } else if (catch_arg(argval, jack_client_name_pfx, argv[i])) {
                strcpy(jack_client_name, argval);
                fprintf(stdout, "Custom JACK client name from arguments: \"%s\"\n", jack_client_name);

            // ports number
            } else if (catch_arg(argval, ports_num_pfx, argv[i])) {
                num_playback_channels = atoi(argval);
                num_capture_channels = atoi(argval);
                fprintf(stdout, "Custom ports number from arguments: %d\n", num_playback_channels);
            } else if (catch_arg(argval, playback_ports_pfx, argv[i])) {
                num_playback_channels = atoi(argval);
                fprintf(stdout, "Custom playback ports number from arguments: %d\n", num_playback_channels);
            } else if (catch_arg(argval, capture_ports_pfx, argv[i])) {
                num_capture_channels = atoi(argval);
                fprintf(stdout, "Custom capture ports number from arguments: %d\n", num_capture_channels);

            // unknown argument
            } else {
                fprintf(stderr, "Unknown argument: \"%s\"\n", argv[i]);
                fprintf(stdout, "%s", usage);
                return EXIT_FAILURE;
            }
        }
    }

    if (init_jack() != 0) {
        fprintf(stderr, "Cannot initialize JACK\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Start main loop...\n");
    sleep(-1);

    return EXIT_SUCCESS;
}
