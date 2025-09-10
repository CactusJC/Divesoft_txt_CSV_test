/*
 * dsf2csv - A simple tool to convert Divesoft Freedom .dsf files to CSV.
 *
 * This tool is a minimalist utility based on libdivecomputer.
 * It reads a .dsf file and outputs its dive data as a CSV file.
 *
 * Copyright (C) 2023 Jules
 *
 * Usage: ./dsf2csv <input.dsf>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdivecomputer/context.h"
#include "libdivecomputer/parser.h"
#include "libdivecomputer/version.h"

// A struct to hold all the data for a single row in the CSV.
typedef struct {
    unsigned int time;
    double depth;
    double temperature;
    double ppo2;
    double cns;
    double setpoint;
    // Separate fields for deco info, since dc_deco_t is not a real type
    unsigned int deco_type;
    unsigned int deco_time;
    double deco_depth;
    int dirty; // Flag to check if we have data to write
} sample_data_t;

// A struct to pass necessary data to the callback function.
typedef struct {
    FILE *outfile;
    sample_data_t *current_sample;
} callback_userdata_t;

// Forward declarations
static void show_help(void);
static dc_status_t read_file_into_buffer(const char *filename, unsigned char **buffer, size_t *size);
static void sample_callback(dc_sample_type_t type, const dc_sample_value_t *value, void *userdata);
static void write_sample_to_csv(FILE *outfile, sample_data_t *sample);
static void reset_sample_data(sample_data_t *sample);

int main(int argc, char *argv[])
{
    // --- Argument Parsing ---
    if (argc != 2) {
        show_help();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        show_help();
        return 0;
    }

    const char *input_filename = argv[1];

    // --- libdivecomputer setup ---
    dc_context_t *context = NULL;
    dc_status_t status = dc_context_new(&context);
    if (status != DC_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create libdivecomputer context (code: %d)\n", status);
        return 1;
    }

    dc_context_set_loglevel(context, DC_LOGLEVEL_WARNING);

    // --- Read input file ---
    unsigned char *file_buffer = NULL;
    size_t file_size = 0;
    status = read_file_into_buffer(input_filename, &file_buffer, &file_size);
    if (status != DC_STATUS_SUCCESS) {
        dc_context_free(context);
        return 1;
    }

    // --- Find Divesoft Freedom descriptor ---
    dc_iterator_t *iter = NULL;
    dc_descriptor_t *descriptor = NULL;
    dc_descriptor_iterator_new(&iter, context);

    while (dc_iterator_next(iter, &descriptor) == DC_STATUS_SUCCESS) {
        if (strcmp(dc_descriptor_get_vendor(descriptor), "Divesoft") == 0 &&
            strcmp(dc_descriptor_get_product(descriptor), "Freedom") == 0) {
            break; // Found it
        }
        dc_descriptor_free(descriptor);
        descriptor = NULL;
    }
    dc_iterator_free(iter);

    if (descriptor == NULL) {
        fprintf(stderr, "Error: Divesoft Freedom descriptor not found in library.\n");
        free(file_buffer);
        dc_context_free(context);
        return 1;
    }

    // --- Create Parser ---
    dc_parser_t *parser = NULL;
    status = dc_parser_new2(&parser, context, descriptor, file_buffer, file_size);
    dc_descriptor_free(descriptor); // Free the descriptor when done

    if (status != DC_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create parser (code: %d). Is this a valid .dsf file?\n", status);
        free(file_buffer);
        dc_context_free(context);
        return 1;
    }

    printf("Successfully created parser for %s.\n\n", input_filename);

    // --- Extract Metadata and Write CSV Header ---
    dc_datetime_t datetime;
    if (dc_parser_get_datetime(parser, &datetime) == DC_STATUS_SUCCESS) {
        printf("Dive Date/Time: %04d-%02d-%02d %02d:%02d:%02d\n",
               datetime.year, datetime.month, datetime.day,
               datetime.hour, datetime.minute, datetime.second);
    }

    double max_depth;
    if (dc_parser_get_field(parser, DC_FIELD_MAXDEPTH, 0, &max_depth) == DC_STATUS_SUCCESS) {
        printf("Max Depth: %.2f m\n", max_depth);
    }

    unsigned int divetime;
    if (dc_parser_get_field(parser, DC_FIELD_DIVETIME, 0, &divetime) == DC_STATUS_SUCCESS) {
        printf("Dive Time: %u min\n", divetime / 60);
    }

    char output_filename[256];
    strncpy(output_filename, input_filename, sizeof(output_filename) - 5);
    output_filename[sizeof(output_filename) - 5] = '\0';
    char *dot = strrchr(output_filename, '.');
    if (dot) {
        strcpy(dot, ".csv");
    } else {
        strcat(output_filename, ".csv");
    }

    FILE *output_file = fopen(output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Error: Could not open output file %s\n", output_filename);
        dc_parser_destroy(parser);
        free(file_buffer);
        dc_context_free(context);
        return 1;
    }

    fprintf(output_file, "Time,Depth,Temperature,PPO2,CNS,Setpoint,DecoType,DecoTime,DecoDepth\n");
    printf("\nCSV file created: %s\n", output_filename);
    printf("Writing sample data...\n");

    // --- Process Samples ---
    sample_data_t current_sample;
    reset_sample_data(&current_sample);

    callback_userdata_t userdata = {
        .outfile = output_file,
        .current_sample = &current_sample
    };

    status = dc_parser_samples_foreach(parser, sample_callback, &userdata);
    if (status != DC_STATUS_SUCCESS) {
        fprintf(stderr, "Error during sample processing (code: %d)\n", status);
    }

    write_sample_to_csv(output_file, &current_sample);

    printf("Sample data written successfully.\n");

    // --- Cleanup ---
    fclose(output_file);
    dc_parser_destroy(parser);
    free(file_buffer);
    dc_context_free(context);

    return 0;
}

static void show_help(void)
{
    printf("dsf2csv - Divesoft Freedom .dsf to CSV Converter\n");
    printf("Version: %s\n\n", DC_VERSION);
    printf("Usage: ./dsf2csv <input.dsf>\n");
    printf("       ./dsf2csv --help\n\n");
    printf("This tool reads a single .dsf file and outputs a .csv file\n");
    printf("with the same name (e.g., my_dive.dsf -> my_dive.csv).\n");
}

static dc_status_t read_file_into_buffer(const char *filename, unsigned char **buffer, size_t *size)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'.\n", filename);
        return DC_STATUS_INVALIDARGS;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (*size == 0) {
        fprintf(stderr, "Error: File '%s' is empty.\n", filename);
        fclose(file);
        return DC_STATUS_INVALIDARGS;
    }

    *buffer = (unsigned char *)malloc(*size);
    if (!*buffer) {
        fprintf(stderr, "Error: Cannot allocate memory to read file.\n");
        fclose(file);
        return DC_STATUS_NOMEMORY;
    }

    if (fread(*buffer, 1, *size, file) != *size) {
        fprintf(stderr, "Error: Failed to read the entire file '%s'.\n", filename);
        fclose(file);
        free(*buffer);
        return DC_STATUS_IO;
    }

    fclose(file);
    return DC_STATUS_SUCCESS;
}

static void reset_sample_data(sample_data_t *sample)
{
    sample->time = 0;
    sample->depth = -1.0;
    sample->temperature = -999.0;
    sample->ppo2 = -1.0;
    sample->cns = -1.0;
    sample->setpoint = -1.0;
    sample->deco_type = DC_DECO_NDL;
    sample->deco_time = 0;
    sample->deco_depth = 0.0;
    sample->dirty = 0;
}

static void write_sample_to_csv(FILE *outfile, sample_data_t *sample)
{
    if (!sample->dirty) return;

    fprintf(outfile, "%u,", sample->time / 1000);

    if (sample->depth >= 0.0) fprintf(outfile, "%.2f", sample->depth);
    fprintf(outfile, ",");

    if (sample->temperature > -999.0) fprintf(outfile, "%.1f", sample->temperature);
    fprintf(outfile, ",");

    if (sample->ppo2 >= 0.0) fprintf(outfile, "%.2f", sample->ppo2);
    fprintf(outfile, ",");

    if (sample->cns >= 0.0) fprintf(outfile, "%.2f", sample->cns);
    fprintf(outfile, ",");

    if (sample->setpoint >= 0.0) fprintf(outfile, "%.2f", sample->setpoint);
    fprintf(outfile, ",");

    const char *deco_type_str = "NDL";
    if (sample->deco_type == DC_DECO_DECOSTOP) deco_type_str = "DECOSTOP";
    else if (sample->deco_type == DC_DECO_SAFETYSTOP) deco_type_str = "SAFETYSTOP";
    fprintf(outfile, "%s,%u,%.2f\n", deco_type_str, sample->deco_time, sample->deco_depth);
}

static void sample_callback(dc_sample_type_t type, const dc_sample_value_t *value, void *userdata)
{
    callback_userdata_t *data = (callback_userdata_t *)userdata;
    sample_data_t *sample = data->current_sample;

    if (type == DC_SAMPLE_TIME) {
        write_sample_to_csv(data->outfile, sample);
        reset_sample_data(sample);
        sample->time = value->time;
        sample->dirty = 1;
    } else if (type == DC_SAMPLE_DEPTH) {
        sample->depth = value->depth;
    } else if (type == DC_SAMPLE_TEMPERATURE) {
        sample->temperature = value->temperature;
    } else if (type == DC_SAMPLE_PPO2) {
        sample->ppo2 = value->ppo2.value;
    } else if (type == DC_SAMPLE_CNS) {
        sample->cns = value->cns;
    } else if (type == DC_SAMPLE_SETPOINT) {
        sample->setpoint = value->setpoint;
    } else if (type == DC_SAMPLE_DECO) {
        sample->deco_type = value->deco.type;
        sample->deco_time = value->deco.time;
        sample->deco_depth = value->deco.depth;
    }
}
