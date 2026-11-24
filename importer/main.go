package main

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"unicode"

	"github.com/SladkyCitron/resona/afmt"
	"github.com/SladkyCitron/resona/codec"
	"github.com/SladkyCitron/resona/codec/wav"
	_ "github.com/SladkyCitron/resona/codec/mp3"
	_ "github.com/SladkyCitron/resona/codec/flac"
	_ "github.com/SladkyCitron/resona/codec/oggvorbis"
)


func find_config_file(zip_file *zip.ReadCloser) (io.ReadCloser, error) {
	return load_file_from_zip("config.json", zip_file)
}

func load_file_from_zip(
	file_path string,
	zip_file *zip.ReadCloser,
) (io.ReadCloser, error) {
	if zip_file == nil {
		return nil, errors.New("zip_file is null")
	}

	for _, file := range zip_file.File {
		if file.Name == file_path {
			return file.Open()
		}
	}

	return nil, errors.New(
		"No `config.json` file not found inside the zip file",
	)
}

func parse_json(json_file io.ReadCloser) (map[string]any, error) {
	var json_data map[string]any

	decoder := json.NewDecoder(json_file)
	err     := decoder.Decode(&json_data)

	if err != nil {
		return nil, err
	}

	return json_data, nil

}

type FromTo struct {
	from float64
	to   float64
}

func keymap_from_definitions(definitions any) (map[string]FromTo, error) {
	defs, ok := definitions.(map[string]any)

	if !ok {
		return nil, errors.New("unexpected error casting the thing")
	}

	key_map := make(map[string]FromTo)

	//definitions is a map of key name to [[from, to]] range
	// the value is a nested array for some reason
	//

	for key, val := range defs {

		value, ok := val.(map[string]any)
		if !ok {
			return nil, errors.New("failed to cast val to map")
		}

		//TODO: surely there's a better way to do this in Go?

		timing, ok := value["timing"]
		if !ok {
			continue //no timing for this key
		}

		timing_arr, ok := timing.([]any)
		if !ok {
			continue //invalid timing array format for this key
		}

		if len(timing_arr) < 1 {
			continue //no data
		}

		timing_range, ok := timing_arr[0].([]any)
		if !ok {
			continue //bad data..?
		}

		key_map[key] = FromTo{
			from: timing_range[0].(float64),
			to:   timing_range[1].(float64),
		}
	}

	return key_map, nil
}

type DecodedAudioFile struct {
	audio_data    []float32
	format        afmt.Format
	sample_format afmt.SampleFormat
	sample_rate   float64
	channels      int
}

func decode_audio_file(audio_file io.ReadCloser) (*DecodedAudioFile, error){
	result := DecodedAudioFile{}
	decoder, _, err := codec.Decode(audio_file)
	if err != nil {
		return &result, fmt.Errorf(
			"failed to decode audio (format may not be supported?): %w",
			err,
		)
	}

	result.format        = decoder.Format()
	result.sample_format = decoder.SampleFormat()
	result.sample_rate   = result.format.SampleRate.Hertz()
	result.channels      = result.format.NumChannels
	result.audio_data    = make([]float32, 0, decoder.Len() * result.channels)

	buffer := make([]float32, 4 * 1024)
	for {
		amount, err := decoder.ReadSamples(buffer)
		if amount > 0 {
			result.audio_data = append(
				result.audio_data, buffer[:amount]...
			)
		}

		if err == io.EOF {
			break
		}

		if err != nil {
			return &result, err
		}
	}

	return &result, nil
}


func slice_audio(decoded_file *DecodedAudioFile, from float64, to float64) ([]float32, error){

	frame_from  := int64(from / 1000 * decoded_file.sample_rate)
	frame_to    := int64(to   / 1000 * decoded_file.sample_rate)
	frame_count := frame_to - frame_from;

	if frame_count <= 0 {
		return nil, errors.New("Invalid timing range")
	}

	sample_from := int(frame_from) * decoded_file.channels
	sample_to   := int(frame_to)   * decoded_file.channels

	return decoded_file.audio_data[sample_from:sample_to], nil
}

func save_audio(
	audio_slice []float32,
	wav_filepath string,
	decoded_file *DecodedAudioFile,
)(error){
	file, err := os.Create(wav_filepath)
	if err != nil {
		return err
	}
	defer file.Close()

	encoder, err := wav.NewEncoder(
		file,
		decoded_file.format,
		decoded_file.sample_format,
		//wav.FormatFloat,
		wav.FormatInt,
	)

	if err != nil {
		return err
	}
	defer encoder.Close()

	_, err = encoder.WriteSamples(audio_slice)
	if err != nil {
		return err
	}

	return encoder.Close()
}

func make_folder_name(text string) string {
	var string_builder strings.Builder
	for _, chr := range strings.ToLower(text) {
		if unicode.IsLetter(chr) || unicode.IsDigit(chr) {
			string_builder.WriteRune(chr)
		} else {
			string_builder.WriteRune('_')
		}
	}

	folder_name := "converted_" + string_builder.String()

	//check duplicated folder
	for i := 2; ; i++ {
		_, err := os.Stat(folder_name)
		if os.IsNotExist(err) {
			break
		}
		folder_name = fmt.Sprintf("%s_%d", folder_name, i)
	}

	return folder_name
}

func print_err(err error){
	fmt.Fprintf(os.Stderr, "ERROR: %v\n", err)
	os.Exit(1)
}

func main(){
	executable_name := os.Args[0]
	if len(os.Args) < 2 {
		fmt.Printf("USAGE: %s <path_to_zip_file>\n", executable_name)
		fmt.Printf("Download MechVibes packs from https://mechvibes.com/sound-packs\n")
			return
	}

	zip_path := os.Args[1]

	zip_file, err := zip.OpenReader(zip_path)
	if err != nil {
		print_err(err)
	}
	defer zip_file.Close()


	config_file, err := find_config_file(zip_file)
	if err != nil {
		print_err(err)
	}
	defer config_file.Close()


	json_data, err := parse_json(config_file)
	if err != nil {
		print_err(err)
	}

	// pretty_json, err := json.MarshalIndent(json_data, "", "  ")
	// if err != nil {
	// 	print_err(err)
	// }
	// fmt.Println(string(pretty_json))

	//things i need to grab from the json file:
	// json_data.audio_file
	// json_data.author
	// json_data.name
	// json_data.definition_method MUST be "single" for now
	// json_data.definitions
	//	for definition in definitions ->
	//  mapkey is the event
	//  mapval contains "timing" array, from to

	pack_name         := json_data["name"].(string)
	pack_author       := json_data["author"].(string)
	audio_file_name   := json_data["audio_file"].(string)
	definition_method := json_data["definition_method"].(string)

	if definition_method != "single" {
		//TODO: support more definition methods
		print_err(fmt.Errorf(
			"Pack has definition_method '%s' but only 'single' is allowed for now.",
			definition_method,
		))
	}

	keymap, err := keymap_from_definitions(json_data["definitions"])

	if err != nil {
		print_err(err)
	}

	fmt.Println("---------------------------")
	fmt.Printf("Pack Name:   %s\n", pack_name)
	fmt.Printf("Author:      %s\n", pack_author)
	fmt.Printf("Audio file:  %s\n", audio_file_name)
	fmt.Printf("Mapped keys: %d\n", len(keymap))
	fmt.Println("---------------------------")

	fmt.Printf("> Loading '%s' from zip file...", audio_file_name);
	audio_file, err := load_file_from_zip(audio_file_name, zip_file)
	if err != nil {
		print_err(err)
	}
	defer audio_file.Close()
	fmt.Println("Ok!");

	fmt.Printf("> Decoding audio file...");
	audio, err := decode_audio_file(audio_file)
	if err != nil {
		print_err(err)
	}
	fmt.Println("Ok!");

	folder_name := make_folder_name(pack_name)
	fmt.Printf("> Creating folder '%s'...", folder_name);
	if err := os.MkdirAll(folder_name, 0755); err != nil {
		print_err(err)
	}
	fmt.Println("Ok!");

	for key_name, timing := range keymap {
		fmt.Printf("> Processing key '%s'...", key_name);

		slice, err := slice_audio(audio, timing.from, timing.to)
		if err != nil {
			print_err(err)
		}

		file_path := filepath.Join(folder_name, key_name + ".wav")
		err = save_audio(slice, file_path, audio)
		if err != nil {
			print_err(err)
		}

		fmt.Println("Ok!");
	}

	fmt.Println("---------------------------")
	fmt.Printf("SUCCESS: converted MechVibes Pack '%s' by '%s'\n", pack_name, pack_author)
	fmt.Printf("         (The new files are in: %s/)\n", folder_name)
	fmt.Println("---------------------------")
	fmt.Println("HINT: to use this, run: larper <folder>")
	fmt.Println(" where <folder> is the path to a folder containing one wav file per key.")
	fmt.Println(" you can mix and match the output of any packages however you like.")


}