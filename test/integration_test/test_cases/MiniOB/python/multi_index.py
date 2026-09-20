import __init__

from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  multi_index_test = TestCase()
  multi_index_test.name = 'multi-index'

  ordinary_group = multi_index_test.add_execution_group('ordinary composite index')
  ordinary_group.add_sql_instruction(
      'CREATE TABLE mi_order(tenant int, code char(8), amount float, created date);',
      expected=ResultString.SUCCESS)
  ordinary_group.add_sql_instruction(
      'CREATE INDEX idx_tenant_code ON mi_order(tenant, code);', expected=ResultString.SUCCESS)
  ordinary_group.add_sql_instruction(
      "INSERT INTO mi_order VALUES "
      "(1, 'a', 10.5, '2024-01-01'), (1, 'b', 20.5, '2024-01-02'), "
      "(2, 'a', 30.5, '2024-01-03'), (1, 'a', 40.5, '2024-01-04');",
      expected=ResultString.SUCCESS)
  ordinary_group.add_block_sql_instruction('''
      SELECT tenant, code, amount FROM mi_order WHERE tenant = 1 AND code = 'a' ORDER BY amount;
      tenant | code | amount
      1 | a | 10.5
      1 | a | 40.5
  ''')
  ordinary_group.add_block_sql_instruction('''
      SELECT count(*) FROM mi_order WHERE tenant = 1;
      count(*)
      3
  ''')

  type_group = multi_index_test.add_execution_group('mixed composite key types')
  type_group.add_sql_instruction(
      'CREATE TABLE mi_types(score float, day date, tag char(6));', expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      'CREATE UNIQUE INDEX uniq_score_day_tag ON mi_types(score, day, tag);', expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      "INSERT INTO mi_types VALUES (1.5, '2024-02-01', 'a'), (1.5, '2024-02-01', 'b');",
      expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      "INSERT INTO mi_types VALUES (1.5, '2024-02-01', 'a');", expected=ResultString.FAILURE)
  type_group.add_block_sql_instruction('''
      SELECT tag FROM mi_types WHERE tag = 'b' AND day = '2024-02-01' AND score = 1.5;
      tag
      b
  ''')

  unique_group = multi_index_test.add_execution_group('unique composite keys')
  unique_group.add_sql_instruction(
      'CREATE TABLE mi_unique(tenant int, code char(8), seq int, day date);', expected=ResultString.SUCCESS)
  unique_group.add_sql_instruction(
      'CREATE UNIQUE INDEX uniq_tenant_code ON mi_unique(tenant, code);', expected=ResultString.SUCCESS)
  unique_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (1, 'a', 1, '2024-01-01'), "
      "(1, 'b', 2, '2024-01-02'), (2, 'a', 3, '2024-01-03');",
      expected=ResultString.SUCCESS)
  unique_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (1, 'a', 4, '2024-01-04');", expected=ResultString.FAILURE)
  unique_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (2, 'b', 4, '2024-01-04');", expected=ResultString.SUCCESS)

  atomic_group = multi_index_test.add_execution_group('atomic insert and update', [unique_group])
  atomic_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (3, 'a', 5, '2024-01-05'), (1, 'b', 6, '2024-01-06');",
      expected=ResultString.FAILURE)
  atomic_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (3, 'a', 5, '2024-01-05');", expected=ResultString.SUCCESS)
  atomic_group.add_sql_instruction(
      "UPDATE mi_unique SET code = 'a' WHERE tenant = 1 AND code = 'b';", expected=ResultString.FAILURE)
  atomic_group.add_sql_instruction(
      'UPDATE mi_unique SET tenant = 4 WHERE tenant = 3;', expected=ResultString.SUCCESS)
  atomic_group.add_sql_instruction(
      "DELETE FROM mi_unique WHERE tenant = 1 AND code = 'a';", expected=ResultString.SUCCESS)
  atomic_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (1, 'a', 7, '2024-01-07');", expected=ResultString.SUCCESS)
  atomic_group.add_block_sql_instruction('''
      SELECT tenant, code, seq FROM mi_unique ORDER BY tenant, code;
      tenant | code | seq
      1 | a | 7
      1 | b | 2
      2 | a | 3
      2 | b | 4
      4 | a | 5
  ''')

  build_group = multi_index_test.add_execution_group('build over existing data')
  build_group.add_sql_instruction(
      'CREATE TABLE mi_build(a int, b int, payload int);', expected=ResultString.SUCCESS)
  build_group.add_sql_instruction(
      'INSERT INTO mi_build VALUES (1, 1, 10), (1, 2, 20), (1, 1, 30);', expected=ResultString.SUCCESS)
  build_group.add_sql_instruction(
      'CREATE UNIQUE INDEX uniq_ab ON mi_build(a, b);', expected=ResultString.FAILURE)
  build_group.add_sql_instruction('DELETE FROM mi_build WHERE payload = 30;', expected=ResultString.SUCCESS)
  build_group.add_sql_instruction(
      'CREATE UNIQUE INDEX uniq_ab ON mi_build(a, b);', expected=ResultString.SUCCESS)
  build_group.add_sql_instruction('INSERT INTO mi_build VALUES (1, 1, 40);', expected=ResultString.FAILURE)
  build_group.add_sql_instruction('INSERT INTO mi_build VALUES (2, 1, 40);', expected=ResultString.SUCCESS)

  validation_group = multi_index_test.add_execution_group('field validation')
  validation_group.add_sql_instruction('CREATE TABLE mi_invalid(a int, b int);', expected=ResultString.SUCCESS)
  validation_group.add_sql_instruction(
      'CREATE INDEX duplicate_fields ON mi_invalid(a, a);', expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'CREATE INDEX missing_field ON mi_invalid(a, missing);', expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'CREATE INDEX valid_fields ON mi_invalid(a, b);', expected=ResultString.SUCCESS)

  persistence_group = multi_index_test.add_execution_group(
      'composite metadata persistence', [ordinary_group, type_group, atomic_group, build_group, validation_group])
  persistence_group.add_instruction(RestartInstruction())
  persistence_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (1, 'a', 8, '2024-01-08');", expected=ResultString.FAILURE)
  persistence_group.add_sql_instruction(
      "INSERT INTO mi_unique VALUES (1, 'c', 8, '2024-01-08');", expected=ResultString.SUCCESS)
  persistence_group.add_sql_instruction('INSERT INTO mi_build VALUES (1, 2, 50);', expected=ResultString.FAILURE)
  persistence_group.add_sql_instruction('INSERT INTO mi_order VALUES (1, \'a\', 50.5, \'2024-01-05\');',
      expected=ResultString.SUCCESS)
  persistence_group.add_block_sql_instruction('''
      SELECT amount FROM mi_order WHERE code = 'a' AND tenant = 1 ORDER BY amount;
      amount
      10.5
      40.5
      50.5
  ''')
  persistence_group.add_sql_instruction(
      "INSERT INTO mi_types VALUES (1.5, '2024-02-01', 'b');", expected=ResultString.FAILURE)

  return multi_index_test
