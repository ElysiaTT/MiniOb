import __init__

from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  unique_test = TestCase()
  unique_test.name = 'unique'

  init_group = unique_test.add_execution_group('unique indexes and initial data')
  init_group.add_sql_instruction(
      'CREATE TABLE unique_table(id int, code char(6), score float, birthday date);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction('CREATE UNIQUE INDEX unique_id ON unique_table(id);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction('CREATE UNIQUE INDEX unique_code ON unique_table(code);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction('CREATE UNIQUE INDEX unique_score ON unique_table(score);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction('CREATE UNIQUE INDEX unique_date ON unique_table(birthday);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES "
      "(1, 'a', 10.5, '2020-01-01'), (2, 'b', 20.5, '2021-01-01');",
      expected=ResultString.SUCCESS)

  conflicts_group = unique_test.add_execution_group('type coverage and index rollback', [init_group])
  conflicts_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (1, 'c', 30.5, '2022-01-01');", expected=ResultString.FAILURE)
  conflicts_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (3, 'a', 30.5, '2022-01-01');", expected=ResultString.FAILURE)
  conflicts_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (3, 'c', 20.5, '2022-01-01');", expected=ResultString.FAILURE)
  conflicts_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (3, 'c', 30.5, '2021-01-01');", expected=ResultString.FAILURE)
  conflicts_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (3, 'c', 30.5, '2022-01-01');", expected=ResultString.SUCCESS)

  atomic_group = unique_test.add_execution_group('multi-row atomicity', [conflicts_group])
  atomic_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES "
      "(4, 'd', 40.5, '2023-01-01'), (5, 'e', 30.5, '2024-01-01');",
      expected=ResultString.FAILURE)
  atomic_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (4, 'd', 40.5, '2023-01-01');", expected=ResultString.SUCCESS)

  update_group = unique_test.add_execution_group('update delete and key reuse', [atomic_group])
  update_group.add_sql_instruction('UPDATE unique_table SET id = 2 WHERE id = 3;', expected=ResultString.FAILURE)
  update_group.add_sql_instruction('UPDATE unique_table SET score = 99 WHERE id >= 3;', expected=ResultString.FAILURE)
  update_group.add_sql_instruction("UPDATE unique_table SET code = 'z' WHERE id = 3;", expected=ResultString.SUCCESS)
  update_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (5, 'c', 50.5, '2024-01-01');", expected=ResultString.SUCCESS)
  update_group.add_sql_instruction('DELETE FROM unique_table WHERE id = 1;', expected=ResultString.SUCCESS)
  update_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (1, 'f', 60.5, '2025-01-01');", expected=ResultString.SUCCESS)
  update_group.add_block_sql_instruction('''
      SELECT id, code, score, birthday FROM unique_table ORDER BY id;
      id | code | score | birthday
      1 | f | 60.5 | 2025-01-01
      2 | b | 20.5 | 2021-01-01
      3 | z | 30.5 | 2022-01-01
      4 | d | 40.5 | 2023-01-01
      5 | c | 50.5 | 2024-01-01
  ''')

  create_group = unique_test.add_execution_group('build over existing duplicates and clean failure', [update_group])
  create_group.add_sql_instruction('CREATE TABLE duplicate_table(seq int, key_value int);', expected=ResultString.SUCCESS)
  create_group.add_sql_instruction('INSERT INTO duplicate_table VALUES (1, 7), (2, 7);', expected=ResultString.SUCCESS)
  create_group.add_sql_instruction(
      'CREATE UNIQUE INDEX duplicate_key ON duplicate_table(key_value);', expected=ResultString.FAILURE)
  create_group.add_sql_instruction('DELETE FROM duplicate_table WHERE seq = 2;', expected=ResultString.SUCCESS)
  create_group.add_sql_instruction(
      'CREATE UNIQUE INDEX duplicate_key ON duplicate_table(key_value);', expected=ResultString.SUCCESS)
  create_group.add_sql_instruction('INSERT INTO duplicate_table VALUES (3, 7);', expected=ResultString.FAILURE)
  create_group.add_sql_instruction('INSERT INTO duplicate_table VALUES (3, 8);', expected=ResultString.SUCCESS)

  ordinary_group = unique_test.add_execution_group('ordinary indexes still allow duplicates', [create_group])
  ordinary_group.add_sql_instruction('CREATE TABLE ordinary_table(id int);', expected=ResultString.SUCCESS)
  ordinary_group.add_sql_instruction('CREATE INDEX ordinary_id ON ordinary_table(id);', expected=ResultString.SUCCESS)
  ordinary_group.add_sql_instruction('INSERT INTO ordinary_table VALUES (1), (1);', expected=ResultString.SUCCESS)
  ordinary_group.add_block_sql_instruction('''
      SELECT id FROM ordinary_table ORDER BY id;
      id
      1
      1
  ''')

  persistence_group = unique_test.add_execution_group('metadata persistence', [ordinary_group])
  persistence_group.add_instruction(RestartInstruction())
  persistence_group.add_sql_instruction(
      "INSERT INTO unique_table VALUES (2, 'g', 70.5, '2026-01-01');", expected=ResultString.FAILURE)
  persistence_group.add_sql_instruction('INSERT INTO duplicate_table VALUES (4, 8);', expected=ResultString.FAILURE)
  persistence_group.add_sql_instruction('INSERT INTO ordinary_table VALUES (1);', expected=ResultString.SUCCESS)

  return unique_test
