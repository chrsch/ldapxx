/*
 * Copyright 2017 Maarten de Vries <maarten@de-vri.es>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include "options.hpp"
#include "types.hpp"

#include <ldap.h>

#include <boost/optional.hpp>

#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace ldapxx {

/// Connection options.
/**
 * When default constructed, only the LDAP protocol version will be set to 3.
 * No other options are set and no STARTTLS handshake will be attempted.
 * An LDAPS URI may still force the use of TLS without STARTTLS.
 */
struct connection_options {
	/// Connection options regarding the LDAP library.
	struct ldap_options {
		boost::optional<int> protocol_version                      = 3;
		boost::optional<int> debug_level                           = boost::none;
		boost::optional<std::string> default_base_bn               = boost::none;
		boost::optional<std::chrono::microseconds> network_timeout = boost::none;
	} ldap;

	/// Connection options regarding TCP.
	struct tcp_options {
		boost::optional<std::chrono::seconds> keepalive_idle;
		boost::optional<std::chrono::seconds> keepalive_interval;
		boost::optional<int> keepalive_probes;
	} tcp;

	/// Connection options regarding TLS.
	struct tls_options {
		bool starttls                                = false;
		boost::optional<require_cert_t> require_cert = boost::none;
		boost::optional<std::string>    cacertdir    = boost::none;
		boost::optional<std::string>    cacertfile   = boost::none;
		boost::optional<std::string>    ciphersuite  = boost::none;
		boost::optional<crl_check_t>    crlcheck     = boost::none;
		boost::optional<std::string>    crlfile      = boost::none;
		boost::optional<std::string>    dhfile       = boost::none;
		boost::optional<std::string>    certfile     = boost::none;
		boost::optional<std::string>    keyfile      = boost::none;
		boost::optional<tls_protocol_t> protocol_min = boost::none;
		boost::optional<std::string>    random_file  = boost::none;
	} tls;
};

/// Get a sane set of default options for an LDAP over TLS connection using an ldaps:// uri.
/**
 * The returned options do not cause a STARTTLS handshake to be perfomed.
 * The connection MUST be opened with an LDAPS URI to use TLS.
 *
 * Alternatively, to force a STARTTLS handshake, see defalt_tls_options().
 */
inline connection_options default_ldaps_options() {
	connection_options result;
	result.tls.require_cert = require_cert_t::demand;
	result.tls.ciphersuite  = "HIGH:!EXPORT:!NULL";
	result.tls.protocol_min = tls_protocol_t::tls1_2;
	return result;
}

/// Get a sane set of default options for a TLS connection using STARTTLS.
inline connection_options default_tls_options() {
	connection_options result = default_ldaps_options();
	result.tls.starttls = true;
	return result;
}

void apply_options(LDAP * connection, connection_options::ldap_options const & options);
void apply_options(LDAP * connection, connection_options::tcp_options  const & options);
void apply_options(LDAP * connection, connection_options::tls_options  const & options);
void apply_options(LDAP * connection, connection_options const & options);

/// A small wrapper around native LDAP connections.
/**
 * Internally, the connection holds only a pointer to a native LDAP object
 * as used by the C API. This means that it is safe to copy the connection.
 *
 * For convenience, the connection is implicitly convertable to the native C handle.
 */
class connection {
	/// The native handle.
	LDAP * ldap_;

public:
	/// Construct a connection wrapper from a raw LDAP pointer.
	/**
	 * No options are set, and no TLS handshake is initiated using this constructor.
	 */
	connection(LDAP * ldap);

	/// Connect to a LDAP server and set options on the connection.
	/**
	 * The options are set just after the connection is initialized,
	 * before the connection is really opened.
	 *
	 * If options.tls.starttls is set, a STARTTLS command will be issued after the connection is opened.
	 *
	 * Note that the LDAP library doesn't actually open a connection until the first action is performed.
	 * The STARTTLS command is a convenient way to force the LDAP library to open the connection.
	 *
	 * For a set of reasonably secure TLS options, you can use default_tls_options().
	 */
	connection(std::string const & uri, connection_options const & options);

	/// Get the native handle usable with the C API.
	LDAP * native() const { return ldap_; }

	/// Allow implicit conversion to the native C API handle.
	operator LDAP * () const { return native(); }

	/// Perform a simple bind with a DN and a password.
	void simple_bind(std::string const & dn, std::string_view password);

	/// Perform a search query.
	/**
	 * The returned result is automatically wrapped in a unique_ptr with the appropriate deleter.
	 */
	owned_result search(
		query const & query,
		std::chrono::milliseconds timeout,
		std::size_t max_response_size = default_max_response_size
	);

	/// Apply a number of modifications to an LDAP entry.
	/**
	 * The modifications are performed in the order specified.
	 */
	void modify(std::string const & dn, std::vector<modification> const & modifications);

	/// Add an attribute value to an LDAP entry.
	/**
	 * The attribute will be created if needed (and if possible).
	 */
	void add_attribute_value(std::string const & dn, std::string const & attribute, std::string_view value);

	/// Delete an attribute value from an LDAP entry.
	void remove_attribute_value(std::string const & dn, std::string const & attribute, std::string_view value);

	/// Delete an attribute from an LDAP entry.
	void remove_attribute(std::string const & dn, std::string const & attribute);

	/// Add an entry to the LDAP directory.
	void add_entry(std::string const & dn, std::map<std::string, std::vector<std::string>> const & attributes);

	/// Delete an entry from the LDAP directory.
	void remove_entry(std::string const & dn);
};

}
