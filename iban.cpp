/*
 * IBAN: PostgreSQL functions and datatype
 * Copyright © 2016 Yorick de Wid <yorick17@outlook.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 *********************************************************************/

extern "C"
{

#include "postgres.h"
#if PG_MAJORVERSION_NUM >= 16
#include "varatt.h"
#endif

#include "utils/builtins.h"
#include "libpq/pqformat.h"

}

#include <regex>

class Specification {
  public:
	Specification(const std::string& structure, size_t length, bool is_sepa)
		: structure{ structure }
		, m_length{ length }
		, is_sepa { is_sepa }
	{
	};
	
	inline size_t getLength() const noexcept
	{
		return m_length;
	}

	inline bool checkRegex(const std::string& str) const
	{
		return std::regex_match(str, structure);
	}

	inline bool isSepa() const {
		return is_sepa;
	}


private:
	std::regex structure;
	size_t m_length;
	bool is_sepa;
};

class Validate {
  public:
	Validate();

	bool isValid(std::string arg) const;
	
	bool isSepaCountry(const std::string& countryCode) const;

	void addSpecification(std::string countryCode, size_t length, std::string structure, bool is_sepa) noexcept
	{
		specifications.emplace(countryCode, std::make_unique<Specification>(structure, length, is_sepa));
	}

private:
	std::map<std::string, std::unique_ptr<Specification>> specifications;
};

/* Validator global instance. */
static const Validate validator;

/**
* Calculates the MOD 97 10 of the passed IBAN as specified in ISO7064.
* The passed in string is assumed to be uppercase
*
* @param string iban
* @returns {bool}
*/
static bool iso7064Mod97_10(std::string iban) {
	std::rotate(iban.begin(), iban.begin() + 4, iban.end());
	
	/* Will contain the letter substitutions */
	std::string numberstring;
	numberstring.reserve(iban.size());

	for (const auto& c : iban) {
		if (std::isdigit(c)) {
			numberstring += c;
		}
		if (std::isupper(c)) {
			numberstring += std::to_string(static_cast<int>(c) - 55);
		}
	}

	/*
	 * Implements a stepwise check for mod 97 in chunks of 9 at the first time
	 * then in chunks of seven prepended by the last mod 97 operation converted
	 * to a string
	 */
	size_t segstart = 0;
	int step = 9;
	std::string prepended;
	long number = 0;
	while (segstart < numberstring.length() - step) {
		number = std::stol(prepended + numberstring.substr(segstart, step));
		int remainder = number % 97;
		prepended = std::to_string(remainder);
		if (remainder < 10) {
			prepended = "0" + prepended;
		}
		segstart = segstart + step;
		step = 7;
	}

	number = std::stol(prepended + numberstring.substr(segstart));
	return number % 97 == 1;
}

bool Validate::isValid(std::string account) const {
	/* Convert uppercase */
	std::transform(account.begin(), account.end(), account.begin(), toupper);

	/* Reject anything too small */
	if (account.length() < 5) {
		return false;
	}

	/* Match on country */
	const std::string& countryCode = account.substr(0, 2);
	const std::string& shortened = account.substr(4);

	const auto specFound = specifications.find(countryCode);
	if (specFound == specifications.end()) {
		return false;
	}

	/* Test accountnumber */
	return specFound->second->getLength() == account.length()
	              && specFound->second->checkRegex(shortened)
	              && iso7064Mod97_10(account);
}

bool Validate::isSepaCountry(const std::string& countryCode) const {
	std::string shortened = countryCode.substr(0, 2);

	if (shortened.length() != 2) {
		return false;
	}

	/* Convert uppercase */
	std::transform(shortened.begin(), shortened.end(), shortened.begin(), toupper);

	const auto specFound = specifications.find(shortened);
	if (specFound == specifications.end()) {
		return false;
	}

	return specFound->second->isSepa();

}

Validate::Validate() {
#include "generated_specs.txt"
}

/**
* Separate CXX and C logic to minimize unexpected or malformed symbols due to
* language conversions. Also demark all exceptions the std++ can throw since
* PostgreSQL is not able to handle them.
*
* @param {string} iban
* @returns {bool}
*/
namespace {

bool account_validate_str(const char *iban) {
	bool result;

	try {
		result = validator.isValid(iban);
	} catch (std::exception& e) {
		elog(ERROR, "%s", e.what());
		return false;
	}

	return result;
}

bool account_validate_text(const text *iban) {
	char *ciban;
	bool result;

	ciban = text_to_cstring(iban);

	result = account_validate_str(ciban);

	pfree(ciban);

	return result;
}

bool sepa_validate_country(const text *txt) {
	char *ccountry;
	bool result = false;

	ccountry = text_to_cstring(txt);

	try {
		result = validator.isSepaCountry(ccountry);
	} catch (std::exception& e) {
		elog(ERROR, "%s", e.what());
		result = false;
	}

	pfree(ccountry);

	return result;
}

} // namespace

extern "C"
{

PG_MODULE_MAGIC;

typedef char Iban;

/**************************************************************************
 * Input/Output functions
 **************************************************************************/

PG_FUNCTION_INFO_V1(ibanin);

Datum
ibanin(PG_FUNCTION_ARGS) {
	char	   *inputText = PG_GETARG_CSTRING(0);

	if (!account_validate_str(inputText))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					errmsg("invalid iban format for value: \"%s\"",
						inputText)));

	PG_RETURN_TEXT_P(cstring_to_text(inputText));
}

/* Convert type output */

PG_FUNCTION_INFO_V1(ibanout);

Datum
ibanout(PG_FUNCTION_ARGS) {
	PG_RETURN_CSTRING(TextDatumGetCString(PG_GETARG_DATUM(0)));
}

/**************************************************************************
 * Binary Input/Output functions
 **************************************************************************/

PG_FUNCTION_INFO_V1(ibanrecv);

Datum
ibanrecv(PG_FUNCTION_ARGS) {
	StringInfo     buf = (StringInfo) PG_GETARG_POINTER(0);
	text           *result;
	Iban           *str;
	int            nbytes;

	str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);

	result = cstring_to_text_with_len(str, nbytes);
	pfree(str);
	PG_RETURN_TEXT_P(result);
}

PG_FUNCTION_INFO_V1(ibansend);

Datum
ibansend(PG_FUNCTION_ARGS) {
	text       *t = PG_GETARG_TEXT_PP(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendtext(&buf, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/* Manually verify a text */

PG_FUNCTION_INFO_V1(iban_validate);

Datum
iban_validate(PG_FUNCTION_ARGS) {
	text     *iban = PG_GETARG_TEXT_P(0);

	bool result = account_validate_text(iban);

	PG_RETURN_BOOL(result);
}

/* Verify, that a country code is in sepa
only checks the first 2 letters and ignores the rest */

PG_FUNCTION_INFO_V1(is_sepa_country);

Datum
is_sepa_country(PG_FUNCTION_ARGS) {
	text     *txt = PG_GETARG_TEXT_P(0);

	bool result = sepa_validate_country(txt);

	PG_RETURN_BOOL(result);
}

} // extern "C"
