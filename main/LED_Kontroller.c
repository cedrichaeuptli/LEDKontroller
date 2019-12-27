/*
 	 LED_Kontroller.c is used for Beat detection and drive the WS2815
 	 Author: Cedric Häuptl
 	 Created: 18.11.2019
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "esp_task.h"
#include "esp_log.h"
#include "board.h"
#include "audio_common.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "esp_vad.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "led_strip/led_strip.h"
#include "fft.h"
#include "headphone_detect.h"


#define AUDIO_SAMPLE_RATE_HZ 44032
#define AUDIO_BUFFER_LENGTH 1024
#define LED_GPIO 22
#define MOSFET_GPIO 14
#define KLINKEN_GPIO 12
#define MODE_BUTTON_GPIO 39
#define REC_BUTTON_GPIO 36
#define LED_STRIP_LENGTH 1200U
#define LED_STRIP_RMT_INTR_NUM 15
#define LED_STRIP_1 13
#define LED_STRIP_2 15
#define FREQUENZYBANDS 8
#define AUDIO_BUFFER_SIZE 42

static const char *TAG = "LED_Kontroller";

TaskHandle_t xBeat_detection_handle;
TaskHandle_t xLED_handle;



extern void xBeat_detection(void *args);
extern void xLED(void *args);

/*
static void oneshot_timer_callback(void* arg)
{

	int64_t time_since_boot = esp_timer_get_time();
}
*/


static struct led_color_t led_strip_buf_1[LED_STRIP_LENGTH];
static struct led_color_t led_strip_buf_2[LED_STRIP_LENGTH];


int8_t ucTakt = 0;
int8_t ucBeat = 0;
int8_t ucColor_change = 0;
int16_t usLED_counter = 0;






void walkinglight(struct led_strip_t *led_strip, struct led_color_t *led_color);
void lightchanger(struct led_strip_t *led_strip, struct led_color_t *led_color,int16_t);
void colorwipe(struct led_strip_t *led_strip, struct led_color_t *led_color);
void white(struct led_strip_t *led_strip, struct led_color_t *led_color);



void app_main()
{


    	esp_log_level_set("*", ESP_LOG_WARN);
    	esp_log_level_set(TAG, ESP_LOG_INFO);


	BaseType_t task_created1 = xTaskCreate(xBeat_detection,
                                            "xBeat_detection",
                                            ESP_TASK_MAIN_STACK+10000,
                                            NULL,
                                            ESP_TASK_PRIO_MIN,
                                            &xBeat_detection_handle);

	BaseType_t task_created2 = xTaskCreate(xLED,
                                            "xLED",
                                            ESP_TASK_MAIN_STACK+1000,
                                            NULL,
                                            ESP_TASK_MAIN_PRIO,
                                            &xLED_handle);



(void)task_created2;
(void)task_created1;



}
/********************************************/
// xBeat_detection ist zuständig für die Takterkennung. Dieser wird mittels 2 methoden den Takt erkennen.
// Einerseits mittels Leistungserhöhung, um den momentaner Takt festellen zu können und andererseits über längerezeit mittels eines FFT um den genauen Takt zu erhalten
//
/********************************************/
void xBeat_detection (void *pvParameters)
{
    audio_pipeline_handle_t xPipeline;
    audio_element_handle_t xI2s_stream_reader, xFilter, xRaw_read;
   // esp_timer_handle_t Timer;

    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    //gpio_pad_select_gpio(MODE_BUTTON_GPIO);
    //gpio_set_direction(MODE_BUTTON_GPIO, GPIO_MODE_INPUT);

    ESP_LOGI(TAG, "Task 1 [ 1 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "Task 1 [ 2 ] Create audio pipeline for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    xPipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(xPipeline);

    ESP_LOGI(TAG, "Task 1 [2.1] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.type = AUDIO_STREAM_READER;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg.i2s_port = 1;
#endif
    xI2s_stream_reader = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "Task 1 [2.2] Create filter to resample audio data");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 48000;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = AUDIO_SAMPLE_RATE_HZ;
    rsp_cfg.dest_ch = 1;
    rsp_cfg.type = AUDIO_CODEC_TYPE_ENCODER;
    xFilter = rsp_filter_init(&rsp_cfg);

    ESP_LOGI(TAG, "Task 1 [2.3] Create raw to receive data");
    raw_stream_cfg_t raw_cfg = {
        .out_rb_size = 1 * 1024,
        .type = AUDIO_STREAM_READER,
    };
    xRaw_read = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "Task 1 [ 3 ] Register all elements to audio pipeline");
    audio_pipeline_register(xPipeline, xI2s_stream_reader, "i2s");
    audio_pipeline_register(xPipeline, xFilter, "filter");
    audio_pipeline_register(xPipeline, xRaw_read, "raw");

    ESP_LOGI(TAG, "Task 1 [ 4 ] Link elements together [codec_chip]-->i2s_stream-->filter-->raw-->[VAD]");
    audio_pipeline_link(xPipeline, (const char *[]) {"i2s", "filter", "raw"}, 3);

    ESP_LOGI(TAG, "Task 1 [ 5 ] Start audio_pipeline");
    audio_pipeline_run(xPipeline);

    ESP_LOGI(TAG, "Task 1 [ 6 ] Start Timer");
    /*const esp_timer_create_args_t oneshot_timer_args =
    {
	.callback = &oneshot_timer_callback,
	.name = "oneshot"
    };
    ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &Timer));*/

    ESP_LOGI(TAG, "Task 1 [ 7 ] Initialize FFT analysis");
    fft_config_t *fft_analysis = fft_init(AUDIO_BUFFER_LENGTH, FFT_REAL, FFT_FORWARD, NULL, NULL);
    fft_config_t *fft_frequenzband1 = fft_init(4096, FFT_REAL, FFT_FORWARD,NULL, NULL);
    for(int u = 0; u < 4096;u++)
	{
		fft_frequenzband1->input[u] =  0;
	}


    int16_t *raw_audio_data = (int16_t *)malloc(AUDIO_BUFFER_LENGTH * sizeof(short));
    if (raw_audio_data == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        goto abort_beat_detection;
    }

	ESP_LOGI(TAG, "Task 1 [ 7 ] Initialize Variable");

	float fusVariance_calc = 0;
	float fusVariance = 0;
	float fusBPM_time = 0;
	float fusBPM_sensitivity = 1.3;
	float fusTime_beetween_beat = 0;
	float fusTime_beetween_color = 0;
	float fusAudioData_FFTAverage_Frequenzband1[2048]={0};
	float fusBiggestvalue = 0;
	double fulAudioData_smallAverage_eachFrequenzband[FREQUENZYBANDS][AUDIO_BUFFER_SIZE]={0};
	double fulSmallAverage_one_frequenzband = 0;
	double fulOneSecAverage_one_frequenzband_calc=0;
	double fulOneSecAverage_one_Frequenzband[FREQUENZYBANDS] = {0};
	int16_t usAudio_buffer_pos = 0;
	int16_t usSamplefreq = 0;
	int16_t usFFTbufferpos = 0;
	int16_t usBPM = 0;
	int16_t usPosition = 0;
	int16_t usColor_counter = 0;
	int64_t ullLasttime_Color = 0;
	int64_t ullLasttime = 0;


    while (1) {

	for (int usRingbufferpos = 0; usRingbufferpos < AUDIO_BUFFER_SIZE; usRingbufferpos++)
	{
		raw_stream_read(xRaw_read, (char *)raw_audio_data, AUDIO_BUFFER_LENGTH * sizeof(short));
		for (int m = 0 ; m < AUDIO_BUFFER_LENGTH ; m++)
		{
			fft_analysis->input[m] =raw_audio_data[m];
		}

		fft_execute(fft_analysis);

		for (int i = 0; i < FREQUENZYBANDS; i++)
		{
			for(int j = 0; j < (AUDIO_BUFFER_LENGTH/2)/FREQUENZYBANDS; j++)
			{
				fulSmallAverage_one_frequenzband += (sqrt(fft_analysis->output[usAudio_buffer_pos]*fft_analysis->output[usAudio_buffer_pos]+fft_analysis->output[usAudio_buffer_pos+1] * fft_analysis->output[usAudio_buffer_pos+1]))/(AUDIO_BUFFER_LENGTH);
				usAudio_buffer_pos += 2;
			}

			fulAudioData_smallAverage_eachFrequenzband[i][usRingbufferpos] = fulSmallAverage_one_frequenzband/((AUDIO_BUFFER_LENGTH/2)/FREQUENZYBANDS);
			fulSmallAverage_one_frequenzband = 0;
		}
		usAudio_buffer_pos = 0;

		usSamplefreq++;

		for (int y = 0; y <FREQUENZYBANDS ; y++)
		{
			for (int z = 0; z < 43 ; z++)
			{
				fulOneSecAverage_one_frequenzband_calc += fulAudioData_smallAverage_eachFrequenzband[y][z];
			}
			fulOneSecAverage_one_Frequenzband[y] = fulOneSecAverage_one_frequenzband_calc/43;
			fulOneSecAverage_one_frequenzband_calc = 0;

		}
		for (int q = 0; q < 43 ; q++)
		{
			fusVariance_calc = (fulAudioData_smallAverage_eachFrequenzband[0][q]-fulOneSecAverage_one_Frequenzband[0])*(fulAudioData_smallAverage_eachFrequenzband[0][q]-fulOneSecAverage_one_Frequenzband[0]);
		}
		fusVariance = fusVariance_calc/43;
		fusBPM_sensitivity = (-0.0025714*fusVariance)+1.5142857;
		if (fusBPM_sensitivity < 1)
		{
			fusBPM_sensitivity = 1;
		}
		if (usFFTbufferpos > 4096)
		{
			usFFTbufferpos = 0;
		}

		fft_frequenzband1->input[usFFTbufferpos] = fulAudioData_smallAverage_eachFrequenzband[0][usRingbufferpos];
		usFFTbufferpos++;

		if (usFFTbufferpos%100 == 0.000)
		{
			fft_execute(fft_frequenzband1);
			for (int k = 0 ; k < 2048 ; k++)
			{
				fusAudioData_FFTAverage_Frequenzband1[k] = sqrt(fft_frequenzband1->output[usPosition]*fft_frequenzband1->output[usPosition]+fft_frequenzband1->output[usPosition+1]*fft_frequenzband1->output[usPosition+1])/4096;
				usPosition += 2;
				if ((fusBiggestvalue < fusAudioData_FFTAverage_Frequenzband1[k])&&(k>80))
				{

					fusBiggestvalue = fusAudioData_FFTAverage_Frequenzband1[k];
					usBPM = k*0.63;

				}
			}
			fusBiggestvalue = 0;
			usPosition = 0;

		}
		if(usBPM > 200)
		{
			usBPM = usBPM/2;
		}

		fusTime_beetween_color = (esp_timer_get_time() - ullLasttime_Color)/1000000.0;
		if ((fusTime_beetween_color > 3) && ( usColor_counter > 50))
		{
			usColor_counter = 0;
			ucColor_change++;
			if (ucColor_change > 2)
			{
				ucColor_change = 0;
			}
		}
		if (fusTime_beetween_color > 5)
		{
			if(clear == 0)
			{
				clear = 1;
				ESP_LOGI(TAG, "Buffer is clear");
			}


			for(int u = 0; u < 4096;u++)
			{
				fft_frequenzband1->input[u] =  0;
			}
		}
		else
		{
			clear = 0;
		}



		if ((((fulOneSecAverage_one_Frequenzband[0] * 1.3)+10) < fulAudioData_smallAverage_eachFrequenzband[0][usRingbufferpos]))
		{
			ullLasttime_Color = esp_timer_get_time();

			if (usBPM > 1)
			{
				fusBPM_time = 60.0/usBPM;

			}
			fusTime_beetween_beat = (esp_timer_get_time() - ullLasttime)/1000000.0;
			if((fusBPM_time-0.1) < fusTime_beetween_beat)
			{
				ucTakt = true;
				usColor_counter++;
				ullLasttime = esp_timer_get_time();
			}
			ucBeat = true;
			gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "Beat detected BPM_average: %d ",usBPM);

			/*if (gpio_get_level(MODE_BUTTON_GPIO) == 0)
			{
				for (int p = 0 ; p < 2048 ; p++)
				{
			    		printf("%f ",AudioData_FFTAverage_Frequenzband1[p]);
				}
				printf("\n \n ");

			}*/


       		}
		else
		{
			gpio_set_level(LED_GPIO, 0);
			ucBeat = false;
		}

	}

}


    free(raw_audio_data);
    raw_audio_data = NULL;

    abort_beat_detection:

    ESP_LOGI(TAG, "Task 1 [ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_terminate(xPipeline);

   // fft_destroy(fft_analysis);
    //fft_destroy(fft_frequenzband1);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(xPipeline);

    audio_pipeline_unregister(xPipeline, xI2s_stream_reader);
    audio_pipeline_unregister(xPipeline, xFilter);
    audio_pipeline_unregister(xPipeline, xRaw_read);

    /* Release all resources */
    audio_pipeline_deinit(xPipeline);
    audio_element_deinit(xI2s_stream_reader);
    audio_element_deinit(xFilter);
    audio_element_deinit(xRaw_read);


}
/********************************************/
// xLED dient zur ansteuerung der LED und um die Muster, welche in den Funktionen sind abzuspielen
//
/********************************************/


void xLED (void *pvParameters)
{
	ESP_LOGI(TAG, "Task 2 [ 1 ] Initialize GPIO");
	gpio_pad_select_gpio(MOSFET_GPIO);
	gpio_set_direction(MOSFET_GPIO, GPIO_MODE_OUTPUT);
	gpio_pulldown_en(MOSFET_GPIO);
	gpio_set_level(MOSFET_GPIO,0);

	gpio_pad_select_gpio(REC_BUTTON_GPIO);
	gpio_set_direction(REC_BUTTON_GPIO, GPIO_MODE_INPUT);

	gpio_pad_select_gpio(MODE_BUTTON_GPIO);
    gpio_set_direction(MODE_BUTTON_GPIO, GPIO_MODE_INPUT);

    ESP_LOGI(TAG, "Task 2 [ 2 ] Initialize LED Stripe");
	struct led_strip_t led_strip = {
		.rgb_led_type = RGB_LED_TYPE_WS2812,
		.rmt_channel = RMT_CHANNEL_1,
		.rmt_interrupt_num = LED_STRIP_RMT_INTR_NUM,
		.gpio = LED_STRIP_2,
		.led_strip_buf_1 = led_strip_buf_1,
		.led_strip_buf_2 = led_strip_buf_2,
		.led_strip_length = LED_STRIP_LENGTH
	};
	led_strip.access_semaphore = xSemaphoreCreateBinary();
        led_strip.update_semaphore = xSemaphoreCreateBinary();
	bool led_init_ok = led_strip_init(&led_strip);
	assert(led_init_ok);

	struct led_color_t led_color = {
		.red = 0,
		.green = 0,
		.blue = 0,
		};
	struct led_color_t led_color_get = {
		.red = 0,
		.green = 0,
		.blue = 0,
		};

	led_strip_clear(&led_strip);
	led_strip_show(&led_strip);

	ESP_LOGI(TAG, "Task 2 [ 3 ] Initialize Variable");
	bool Ausschalten = false;
	bool Flankenerkennung = true;
	int8_t Flanke_ModeButton = 0;
	int8_t Modus = 0;
	int8_t LED_white = 0;

	while(true)
	{
		if ((gpio_get_level(REC_BUTTON_GPIO) == 1) && (Flankenerkennung == true))
		{
			Flankenerkennung = false;
			if (Ausschalten == true)
			{
				Ausschalten = false;
			}
			else
			{
				Ausschalten = true;
			}

		}
		if (gpio_get_level(REC_BUTTON_GPIO) == 0)
		{
			Flankenerkennung = true;
		}
		if ((gpio_get_level(MODE_BUTTON_GPIO) == 0) && (Flanke_ModeButton == 1))
		{
			Modus++;
			if (Modus > 4)
			{
				Modus = 0;
			}
		}
		Flanke_ModeButton = gpio_get_level(MODE_BUTTON_GPIO);

		if (Ausschalten == true)
		{
			led_strip_clear(&led_strip);
			led_strip_show(&led_strip);
			vTaskDelay(100/ portTICK_PERIOD_MS);
			gpio_set_level(MOSFET_GPIO,0);
		}
		else
		{
			gpio_set_level(MOSFET_GPIO,1);


			switch(Modus)
			{
				case 0: LED_white = 0;walkinglight(&led_strip,&led_color);break;
				case 1: lightchanger(&led_strip,&led_color,1);break;
				case 2: colorwipe(&led_strip,&led_color);break;
				case 3:
					if(LED_white < 2)
					{
						LED_white++;
						white(&led_strip,&led_color);
					}break;
				case 4:
					if(BPM < 100)
					{
						colorwipe(&led_strip,&led_color);
					}
					else if(BPM < 120)
					{
						lightchanger(&led_strip,&led_color,3);
					}
					else
					{
						lightchanger(&led_strip,&led_color,5);
					}break;
				default:break;
			}
			//vTaskDelay(5/ portTICK_PERIOD_MS);




		}

	}


}
/********************************************/
// Walkinglight wird bei jeder Leistungerhöhung das erste LED einschalten und dann weiterleiten
//
/********************************************/
void walkinglight(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	for (int g = LED_STRIP_LENGTH-1; g > 0;g--)
	{
		led_strip_get_pixel_color(led_strip, g-1, led_color);
		led_strip_set_pixel_color(led_strip, g, led_color);
	}
	switch(ucColor_change)
	{
		case 0: if (ucBeat == true)
			{
				led_color->blue = 200;
			}
			else
			{
				led_color->blue = 0;
			}
			led_color->green = 0;
			led_color->red =0;
			break;
		case 1: if (ucBeat == true)
			{
				led_color->red = 200;
			}
			else
			{
				led_color->red = 0;
			}
			led_color->green = 0;
			led_color->blue =0;
			break;
		case 2: if (ucBeat == true)
			{
				led_color->green = 200;
			}
			else
			{
				led_color->green = 0;
			}
			led_color->red = 0;
			led_color->blue =0;
			break;
		default:break;
	}

	led_strip_set_pixel_color(led_strip, 0, led_color);
	led_strip_show(led_strip);
	vTaskDelay(20/ portTICK_PERIOD_MS);


}
/********************************************/
// Rainbow
//
/********************************************/
void Rainbow(struct led_strip_t *led_strip, struct led_color_t *led_color)
{

	led_strip_set_pixel_color(led_strip, 0, led_color);
	led_strip_show(led_strip);


}
/********************************************/
// colorwipe wird die LED in einem Farbduchlauf abspielen, sobald ein Takt erkannt wird wird die Farbe geändert
//
/********************************************/
void colorwipe(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	usLED_counter++;

	if (ucTakt == true)
	{
		if(usLED_counter > 550)
		{
			usLED_counter -= 550;
		}
		else
		{
			usLED_counter += 50;
		}
	}
	ucTakt = false;
	if(usLED_counter > 600)
	{
		usLED_counter = 0;
	}

	if(usLED_counter<200)
	{
		led_color->red = usLED_counter;
		led_color->blue = 200-usLED_counter;
	}
	if((usLED_counter>200) && (usLED_counter< 400))
	{
		led_color->green = usLED_counter-200;
		led_color->red = 400-usLED_counter;
	}
	if((usLED_counter>400) && (usLED_counter< 600))
	{
		led_color->blue = usLED_counter-400;
		led_color->green = 600-usLED_counter;
	}
	for(int g = 0; g < LED_STRIP_LENGTH;g++)
	{
		led_strip_set_pixel_color(led_strip, g, led_color);
	}
	led_strip_show(led_strip);
	vTaskDelay(20/ portTICK_PERIOD_MS);

}

/********************************************/
// lightchanger wird die LED in Segmente aufteile welche in verschiedenen Farben leuchten sobald ein Takt erkannt wird, änder sich die Farbe
//
/********************************************/


void lightchanger(struct led_strip_t *led_strip, struct led_color_t *led_color,int16_t subdivision)
{

			int16_t LED_subdivision = LED_STRIP_LENGTH/subdivision;
			if (ucTakt == true)
			{
				changing = 1;
				Takt = false;
				for(int index = 0; index < (LED_STRIP_LENGTH/LED_subdivision);index++)
				{
					switch(Color_change)
					{
						case 0: if (led_color->blue == 200)
							{
								led_color->blue = 0;
								led_color->green = 200;
							}
							else
							{
								led_color->blue = 200;
								led_color->green = 0;
							}
							led_color->red =0;
							break;
						case 1: if (led_color->red == 200)
							{
								led_color->red = 0;
								led_color->blue = 200;
							}
							else
							{
								led_color->red = 200;
								led_color->blue = 0;
							}
							led_color->green =0;
							break;
						case 2: if (led_color->green == 200)
							{
								led_color->green = 0;
								led_color->red = 200;
							}
							else
							{
								led_color->green = 200;
								led_color->red = 0;
							}
							led_color->blue =0;
							break;
						default:break;
					}
					for(int indexsub = 0; indexsub < LED_subdivision;indexsub++)
					{
						led_strip_set_pixel_color(led_strip, indexsub+(index*LED_subdivision), led_color);
					}
				}
				led_strip_show(led_strip);

			}
			vTaskDelay(10/ portTICK_PERIOD_MS);




}


void white(struct led_strip_t *led_strip, struct led_color_t *led_color)
{

			led_color->blue = 255;
			led_color->red = 255;
			led_color->green = 255;

				for(int f = 0; f < LED_STRIP_LENGTH;f++)
				{
					led_strip_set_pixel_color(led_strip, f, led_color);
				}
				led_strip_show(led_strip);


			ucTakt = false;
			vTaskDelay(10/ portTICK_PERIOD_MS);



}




