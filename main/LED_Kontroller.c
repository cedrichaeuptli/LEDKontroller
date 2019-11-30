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


#define AUDIO_SAMPLE_RATE_HZ 44032
#define AUDIO_BUFFER_LENGTH 1024
#define LED_GPIO 22
#define MOSFET_GPIO 14
#define KLINKEN_GPIO 12
#define MODE_BUTTON_GPIO 39
#define REC_BUTTON_GPIO 36
#define LED_STRIP_LENGTH 300U
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


int8_t Takt = 0; 
int8_t Beat = 0;
int8_t Color_change = 0;





void walkinglight(struct led_strip_t *led_strip, struct led_color_t *led_color);
void lightchanger(struct led_strip_t *led_strip, struct led_color_t *led_color);


void app_main()
{


    	esp_log_level_set("*", ESP_LOG_WARN);
    	esp_log_level_set(TAG, ESP_LOG_INFO);


	BaseType_t task_created1 = xTaskCreate(xBeat_detection,
                                            "xBeat_detection",
                                            ESP_TASK_MAIN_STACK+100000,
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

void xBeat_detection (void *pvParameters)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t i2s_stream_reader, filter, raw_read;
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
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "Task 1 [2.1] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.type = AUDIO_STREAM_READER;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg.i2s_port = 1;
#endif
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "Task 1 [2.2] Create filter to resample audio data");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 48000;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = AUDIO_SAMPLE_RATE_HZ;
    rsp_cfg.dest_ch = 1;
    rsp_cfg.type = AUDIO_CODEC_TYPE_ENCODER;
    filter = rsp_filter_init(&rsp_cfg);

    ESP_LOGI(TAG, "Task 1 [2.3] Create raw to receive data");
    raw_stream_cfg_t raw_cfg = {
        .out_rb_size = 1 * 1024,
        .type = AUDIO_STREAM_READER,
    };
    raw_read = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "Task 1 [ 3 ] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, filter, "filter");
    audio_pipeline_register(pipeline, raw_read, "raw");

    ESP_LOGI(TAG, "Task 1 [ 4 ] Link elements together [codec_chip]-->i2s_stream-->filter-->raw-->[VAD]");
    audio_pipeline_link(pipeline, (const char *[]) {"i2s", "filter", "raw"}, 3);

    ESP_LOGI(TAG, "Task 1 [ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

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
	float Variance_calc = 0;
	float Variance = 0;
	float BPM_time = 0;
	float BPM_sensitivity = 1.3;
	float time_beetween_beat = 0;
	float time_beetween_color = 0;
	float AudioData_FFTAverage_Frequenzband1[2048]={0};
	float Biggestvalue = 0;
	double AudioData_smallAverage_eachFrequenzband[FREQUENZYBANDS][AUDIO_BUFFER_SIZE]={0};
	double SmallAverage_one_frequenzband = 0;
	double OneSecAverage_one_frequenzband_calc=0;
	double OneSecAverage_one_Frequenzband[FREQUENZYBANDS] = {0};
	int16_t Audio_buffer_pos = 0;
	int16_t samplefreq = 0;
	int16_t FFTbufferpos = 0;
	int16_t BPM = 0;
	int16_t Position = 0;
	int16_t Color_counter = 0;
	int64_t lasttime_Color = 0;
	int64_t lasttime = 0;
	
    while (1) {

	for (int Ringbufferpos = 0; Ringbufferpos < AUDIO_BUFFER_SIZE; Ringbufferpos++)
	{
		raw_stream_read(raw_read, (char *)raw_audio_data, AUDIO_BUFFER_LENGTH * sizeof(short));
		for (int m = 0 ; m < AUDIO_BUFFER_LENGTH ; m++)
		{
			fft_analysis->input[m] =raw_audio_data[m];
		}

		fft_execute(fft_analysis);

		for (int i = 0; i < FREQUENZYBANDS; i++)
		{
			for(int j = 0; j < (AUDIO_BUFFER_LENGTH/2)/FREQUENZYBANDS; j++)
			{
				SmallAverage_one_frequenzband += (sqrt(fft_analysis->output[Audio_buffer_pos]*fft_analysis->output[Audio_buffer_pos]+fft_analysis->output[Audio_buffer_pos+1] * fft_analysis->output[Audio_buffer_pos+1]))/(AUDIO_BUFFER_LENGTH);
				Audio_buffer_pos += 2;
			}
			 
			AudioData_smallAverage_eachFrequenzband[i][Ringbufferpos] = SmallAverage_one_frequenzband/((AUDIO_BUFFER_LENGTH/2)/FREQUENZYBANDS);
			SmallAverage_one_frequenzband = 0;
		}
		Audio_buffer_pos = 0;

		samplefreq++;

		for (int y = 0; y <FREQUENZYBANDS ; y++)	
		{
			for (int z = 0; z < 43 ; z++)
			{
				OneSecAverage_one_frequenzband_calc += AudioData_smallAverage_eachFrequenzband[y][z];			
			}
			OneSecAverage_one_Frequenzband[y] = OneSecAverage_one_frequenzband_calc/43;
			OneSecAverage_one_frequenzband_calc = 0;
			
		}
		for (int q = 0; q < 43 ; q++)
		{
			Variance_calc = (AudioData_smallAverage_eachFrequenzband[0][q]-OneSecAverage_one_Frequenzband[0])*(AudioData_smallAverage_eachFrequenzband[0][q]-OneSecAverage_one_Frequenzband[0]);
		}
		Variance = Variance_calc/43;
		BPM_sensitivity = (-0.0025714*Variance)+1.5142857;
		if (BPM_sensitivity < 1)
		{
			BPM_sensitivity = 1;
		}
		if (FFTbufferpos > 4096)
		{
			FFTbufferpos = 0;
		}
		
		fft_frequenzband1->input[FFTbufferpos] = AudioData_smallAverage_eachFrequenzband[0][Ringbufferpos];
		FFTbufferpos++;

		if (FFTbufferpos%100 == 0.000)
		{
			fft_execute(fft_frequenzband1);
			for (int k = 0 ; k < 2048 ; k++)
			{
				AudioData_FFTAverage_Frequenzband1[k] = sqrt(fft_frequenzband1->output[Position]*fft_frequenzband1->output[Position]+fft_frequenzband1->output[Position+1]*fft_frequenzband1->output[Position+1])/4096;
				Position += 2;
				if ((Biggestvalue < AudioData_FFTAverage_Frequenzband1[k])&&(k>80))
				{
					Biggestvalue = AudioData_FFTAverage_Frequenzband1[k];
					BPM = k*0.63;
				}
			}
			Biggestvalue = 0;
			Position = 0;
		
		}
		if(BPM > 200)
		{
			BPM = BPM/2;
		}

		time_beetween_color = (esp_timer_get_time() - lasttime_Color)/1000000.0;
		if ((time_beetween_color > 3) && ( Color_counter > 50))
		{
			Color_counter = 0;
			Color_change++;
			if (Color_change > 2)
			{
				Color_change = 0;
			}
		}
		if (time_beetween_color > 5)
		{	
			for(int u = 0; u < 4096;u++)
			{
				fft_frequenzband1->input[u] =  0;
			}
		}
		
		
		if ((((OneSecAverage_one_Frequenzband[0] * 1.3)+10) < AudioData_smallAverage_eachFrequenzband[0][Ringbufferpos])) 
		{
			lasttime_Color = esp_timer_get_time();
			
			if (BPM > 1)
			{
				BPM_time = 60.0/BPM;
				
			}
			time_beetween_beat = (esp_timer_get_time() - lasttime)/1000000.0;
			if((BPM_time-0.1) < time_beetween_beat)
			{
				Takt = true;
				Color_counter++;
				lasttime = esp_timer_get_time();
			}
			Beat = true;
			gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "Beat detected BPM_average: %d ",BPM);
			
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
			Beat = false;
		}
		
	}

}


    free(raw_audio_data);
    raw_audio_data = NULL;

    abort_beat_detection:

    ESP_LOGI(TAG, "Task 1 [ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_terminate(pipeline);

   // fft_destroy(fft_analysis);
    //fft_destroy(fft_frequenzband1);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, filter);
    audio_pipeline_unregister(pipeline, raw_read);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(filter);
    audio_element_deinit(raw_read);


}


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
	bool led_init_ok = led_strip_init(&led_strip);
	assert(led_init_ok);

	struct led_color_t led_color = {
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
			if (Modus > 3)
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
				case 0: walkinglight(&led_strip,&led_color);break;
				case 1: lightchanger(&led_strip,&led_color);break;
			
				default:break;
			}
			vTaskDelay(50/ portTICK_PERIOD_MS);
		
						


		}
		
	}


}

void walkinglight(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	for (int g = LED_STRIP_LENGTH-1; g > 0;g--)
	{
		led_strip_get_pixel_color(led_strip, g-1, led_color);
		led_strip_set_pixel_color(led_strip, g, led_color);
	}
	switch(Color_change)
	{
		case 0: if (Beat == true)
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
		case 1: if (Beat == true)
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
		case 2: if (Beat == true)
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
	
	
}

void Rainbow(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	
	led_strip_set_pixel_color(led_strip, 0, led_color);
	led_strip_show(led_strip);
	
	
}

void colorwipe(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	for( int g = 0; g < 765; g++)
	{
	
	}
	led_strip_set_pixel_color(led_strip, 0, led_color);
	led_strip_show(led_strip);
	
	
}

void lightchanger(struct led_strip_t *led_strip, struct led_color_t *led_color)
{
	
			if (Takt == true)
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
				
				for(int g = 0; g < LED_STRIP_LENGTH;g++)
				{
					led_strip_set_pixel_color(led_strip, g, led_color);
				}
				led_strip_show(led_strip);
			}
			
			Takt = false;
			
}

