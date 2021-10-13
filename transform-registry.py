import csv
import re

COUNTRY_CODE_IDX = 2
IS_SEPA_IDX = 4
IBAN_LENGTH_IDX = 19
IBAN_STRUCT_IDX = 18

# maybe it's not latin-1? It's definitely not UTF-8 though
with open('swift_iban_registry_txt.txt', encoding='latin-1') as f:
  data = list(csv.reader(f, delimiter='\t'))

def swift_format_to_re_str(s: str) -> str:
  assert s[:2].isalpha() # countrycode
  assert s[2:5] == '2!n' # checksum
  char_to_re = {
    "n": "0-9",
    "a": "A-Z",
    "c": "A-Za-z0-9",
    # e would be space, not used
  }
  # make sure the swift format is valid
  full_re = re.compile('^([0-9]+![nac])+$')
  assert full_re.match(s[5:])
  swift_form_re = re.compile('([0-9]+)!([nac])')
  re_str = '^'
  for (count, typ) in swift_form_re.findall(s[5:]):
    re_str += f"[{char_to_re[typ]}]{{{count}}}"
  return re_str + '$'

# country_code, (regex, length, is_sepa)
parsed = {}
for country_code, iban_length, iban_struct, is_sepa in zip(data[COUNTRY_CODE_IDX], data[IBAN_LENGTH_IDX], data[IBAN_STRUCT_IDX], data[IS_SEPA_IDX]):
  # skip the "header"
  if country_code != 'IBAN prefix country code (ISO 3166)':
    assert country_code == iban_struct[:2], country_code
    assert not 'e' in iban_struct # would be space, unused
    parsed[country_code] = (swift_format_to_re_str(iban_struct), iban_length, is_sepa == 'Yes')

with open('generated_specs.txt','w') as f:
  # iban specifications
  for country_code, (re, length, is_sepa) in parsed.items():
    f.write(f'addSpecification("{country_code}", {length}, "{re}", {str(is_sepa).lower()});\n')