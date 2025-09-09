#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdivecomputer/context.h>
#include <libdivecomputer/device.h>
#include <libdivecomputer/parser.h>
#include <libdivecomputer/error.h>

static void print_error(const char *msg, dc_status_t rc) {
    fprintf(stderr, "%s (%d)\n", msg, rc);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Gebruik: %s input.dsf output.csv\n", argv[0]);
        return 1;
    }

    const char *inputfile = argv[1];
    const char *outputfile = argv[2];

    FILE *in = fopen(inputfile, "rb");
    if (!in) {
        perror("Kan inputbestand niet openen");
        return 1;
    }

    FILE *out = fopen(outputfile, "w");
    if (!out) {
        perror("Kan outputbestand niet openen");
        fclose(in);
        return 1;
    }

    fprintf(out, "Time (s),Depth (m),Temperature (°C),pO2 (bar)\n");

    dc_context_t *context = NULL;
    dc_status_t rc = dc_context_new(&context);
    if (rc != DC_STATUS_SUCCESS) {
        print_error("Kon geen context maken", rc);
        fclose(in);
        fclose(out);
        return 1;
    }

    dc_parser_t *parser = NULL;
    rc = dc_parser_new(&parser, DC_FAMILY_DIVESOFT, context);
    if (rc != DC_STATUS_SUCCESS) {
        print_error("Parser kon niet aangemaakt worden", rc);
        dc_context_free(context);
        fclose(in);
        fclose(out);
        return 1;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    rewind(in);
    unsigned char *data = (unsigned char *) malloc(size);
    fread(data, 1, size, in);
    fclose(in);

    rc = dc_parser_set_data(parser, data, size);
    if (rc != DC_STATUS_SUCCESS) {
        print_error("Kon parser data niet zetten", rc);
        free(data);
        dc_parser_destroy(parser);
        dc_context_free(context);
        fclose(out);
        return 1;
    }

    dc_sample_value_t sample;
    while ((rc = dc_parser_get_sample(parser, &sample)) == DC_STATUS_SUCCESS) {
        switch (sample.type) {
            case DC_SAMPLE_TIME:
                fprintf(out, "%u,", sample.time);
                break;
            case DC_SAMPLE_DEPTH:
                fprintf(out, "%.2f,", sample.depth);
                break;
            case DC_SAMPLE_TEMPERATURE:
                fprintf(out, "%.2f,", sample.temperature);
                break;
            case DC_SAMPLE_PO2:
                fprintf(out, "%.2f\n", sample.po2);
                break;
            default:
                break;
        }
    }

    if (rc != DC_STATUS_DONE) {
        print_error("Fout tijdens uitlezen samples", rc);
    }

    free(data);
    dc_parser_destroy(parser);
    dc_context_free(context);
    fclose(out);

    printf("CSV export klaar: %s\n", outputfile);
    return 0;
}