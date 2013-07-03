/*
 * libopenmpt_cxx.cpp
 * ------------------
 * Purpose: libopenmpt C++ interface implementation
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#include "BuildSettings.h"

#include "libopenmpt_internal.h"
#include "libopenmpt.hpp"

#include "libopenmpt_impl.hpp"

#include <algorithm>
#include <stdexcept>

#ifndef NO_LIBOPENMPT_CXX

namespace openmpt {

exception::exception( const std::string & text ) : std::runtime_error( text ) {
	return;
}

std::uint32_t get_library_version() {
	return openmpt::version::get_library_version();
}

std::uint32_t get_core_version() {
	return openmpt::version::get_core_version();
}

namespace string {

std::string get( const std::string & key ) {
	return openmpt::version::get_string( key );
}

} // namespace string

std::vector<std::string> get_supported_extensions() {
	return openmpt::module_impl::get_supported_extensions();
}

bool is_extension_supported( const std::string & extension ) {
	return openmpt::module_impl::is_extension_supported( extension );
}

double could_open_propability( std::istream & stream, double effort, std::ostream & log, const detail::api_version_checker & ) {
	return openmpt::module_impl::could_open_propability( stream, effort, std::make_shared<std_ostream_log>( log ) );
}

module::module( const module & ) {
	throw std::runtime_error("openmpt::module is non-copyable");
}

void module::operator = ( const module & ) {
	throw std::runtime_error("openmpt::module is non-copyable");
}

module::module() : impl(0) {
	return;
}

void module::set_impl( module_impl * i ) {
	impl = i;
}

module::module( std::istream & stream, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( stream, std::make_shared<std_ostream_log>( log ) );
}

module::module( const std::vector<std::uint8_t> & data, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( data, std::make_shared<std_ostream_log>( log ) );
}

module::module( const std::uint8_t * beg, const std::uint8_t * end, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( beg, end - beg, std::make_shared<std_ostream_log>( log ) );
}

module::module( const std::vector<char> & data, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( data, std::make_shared<std_ostream_log>( log ) );
}

module::module( const char * beg, const char * end, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( beg, end - beg, std::make_shared<std_ostream_log>( log ) );
}

module::module( const char * data, std::size_t size, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( data, size, std::make_shared<std_ostream_log>( log ) );
}

module::module( const void * data, std::size_t size, std::ostream & log, const detail::api_version_checker & ) : impl(0) {
	impl = new module_impl( data, size, std::make_shared<std_ostream_log>( log ) );
}

module::~module() {
	delete impl;
	impl = 0;
}

std::int32_t module::get_render_param( int param ) const {
	return impl->get_render_param( param );
}

void module::set_render_param( int param, std::int32_t value ) {
	impl->set_render_param( param, value );
}

void module::select_subsong( std::int32_t subsong ) {
	impl->select_subsong( subsong );
}

void module::set_repeat_count( std::int32_t repeat_count ) {
	impl->set_repeat_count( repeat_count );
}
std::int32_t module::get_repeat_count() const {
	return impl->get_repeat_count();
}

double module::seek_seconds( double seconds ) {
	return impl->seek_seconds( seconds );
}

std::size_t module::read( std::int32_t samplerate, std::size_t count, std::int16_t * mono ) {
	return impl->read( samplerate, count, mono );
}
std::size_t module::read( std::int32_t samplerate, std::size_t count, std::int16_t * left, std::int16_t * right ) {
	return impl->read( samplerate, count, left, right );
}
std::size_t module::read( std::int32_t samplerate, std::size_t count, std::int16_t * left, std::int16_t * right, std::int16_t * rear_left, std::int16_t * rear_right ) {
	return impl->read( samplerate, count, left, right, rear_left, rear_right );
}
std::size_t module::read( std::int32_t samplerate, std::size_t count, float * mono ) {
	return impl->read( samplerate, count, mono );
}
std::size_t module::read( std::int32_t samplerate, std::size_t count, float * left, float * right ) {
	return impl->read( samplerate, count, left, right );
}
std::size_t module::read( std::int32_t samplerate, std::size_t count, float * left, float * right, float * rear_left, float * rear_right ) {
	return impl->read( samplerate, count, left, right, rear_left, rear_right );
}
std::size_t module::read_interleaved_stereo( std::int32_t samplerate, std::size_t count, std::int16_t * interleaved_stereo ) {
	return impl->read_interleaved_stereo( samplerate, count, interleaved_stereo );
}
std::size_t module::read_interleaved_quad( std::int32_t samplerate, std::size_t count, std::int16_t * interleaved_quad ) {
	return impl->read_interleaved_quad( samplerate, count, interleaved_quad );
}
std::size_t module::read_interleaved_stereo( std::int32_t samplerate, std::size_t count, float * interleaved_stereo ) {
	return impl->read_interleaved_stereo( samplerate, count, interleaved_stereo );
}
std::size_t module::read_interleaved_quad( std::int32_t samplerate, std::size_t count, float * interleaved_quad ) {
	return impl->read_interleaved_quad( samplerate, count, interleaved_quad );
}

double module::get_current_position_seconds() const {
	return impl->get_current_position_seconds();
}

double module::get_duration_seconds() const {
	return impl->get_duration_seconds();
}

std::vector<std::string> module::get_metadata_keys() const {
	return impl->get_metadata_keys();
}
std::string module::get_metadata( const std::string & key ) const {
	return impl->get_metadata( key );
}

std::int32_t module::get_current_speed() const {
	return impl->get_current_speed();
}
std::int32_t module::get_current_tempo() const {
	return impl->get_current_tempo();
}
std::int32_t module::get_current_order() const {
	return impl->get_current_order();
}
std::int32_t module::get_current_pattern() const {
	return impl->get_current_pattern();
}
std::int32_t module::get_current_row() const {
	return impl->get_current_row();
}
std::int32_t module::get_current_playing_channels() const {
	return impl->get_current_playing_channels();
}

std::int32_t module::get_num_subsongs() const {
	return impl->get_num_subsongs();
}
std::int32_t module::get_num_channels() const {
	return impl->get_num_channels();
}
std::int32_t module::get_num_orders() const {
	return impl->get_num_orders();
}
std::int32_t module::get_num_patterns() const {
	return impl->get_num_patterns();
}
std::int32_t module::get_num_instruments() const {
	return impl->get_num_instruments();
}
std::int32_t module::get_num_samples() const {
	return impl->get_num_samples();
}

std::vector<std::string> module::get_subsong_names() const {
	return impl->get_subsong_names();
}
std::vector<std::string> module::get_channel_names() const {
	return impl->get_channel_names();
}
std::vector<std::string> module::get_order_names() const {
	return impl->get_order_names();
}
std::vector<std::string> module::get_pattern_names() const {
	return impl->get_pattern_names();
}
std::vector<std::string> module::get_instrument_names() const {
	return impl->get_instrument_names();
}
std::vector<std::string> module::get_sample_names() const {
	return impl->get_sample_names();
}

std::int32_t module::get_order_pattern( std::int32_t order ) const {
	return impl->get_order_pattern( order );
}
std::int32_t module::get_pattern_num_rows( std::int32_t pattern ) const {
	return impl->get_pattern_num_rows( pattern );
}

std::uint8_t module::get_pattern_row_channel_command( std::int32_t pattern, std::int32_t row, std::int32_t channel, int command ) const {
	return impl->get_pattern_row_channel_command( pattern, row, channel, command );
}

std::vector<std::string> module::get_ctls() const {
	return impl->get_ctls();
}
std::string module::ctl_get_string( const std::string & ctl ) const {
	return impl->ctl_get_string( ctl );
}
double module::ctl_get_double( const std::string & ctl ) const {
	return impl->ctl_get_double( ctl );
}
std::int64_t module::ctl_get_int64( const std::string & ctl ) const {
	return impl->ctl_get_int64( ctl );
}
void module::ctl_set( const std::string & ctl, const std::string & value ) {
	impl->ctl_set( ctl, value );
}
void module::ctl_set( const std::string & ctl, double value ) {
	impl->ctl_set( ctl, value );
}
void module::ctl_set( const std::string & ctl, std::int64_t value ) {
	impl->ctl_set( ctl, value );
}

} // namespace openmpt

#endif // NO_LIBOPENMPT_CXX
