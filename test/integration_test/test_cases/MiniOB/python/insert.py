import __init__

from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  insert_test = TestCase()
  insert_test.name = 'insert'

  insert_group = insert_test.add_execution_group('multi-row insert')
  insert_group.add_sql_instruction(
      'CREATE TABLE insert_table(id int, name char(4), score int, birthday date);',
      expected=ResultString.SUCCESS)
  insert_group.add_sql_instruction(
      "INSERT INTO insert_table VALUES "
      "(1, 'a', 10, '2024-02-29'), (2, 'b', 20, '2025-1-2'), (3, 'c', 30, '2000-02-29');",
      expected=ResultString.SUCCESS)
  insert_group.add_sort_block_sql_instruction('''
      SELECT * FROM insert_table;
      id | name | score | birthday
      1 | a | 10 | 2024-02-29
      2 | b | 20 | 2025-01-02
      3 | c | 30 | 2000-02-29
  ''')

  compatibility_group = insert_test.add_execution_group('single-row compatibility', [insert_group])
  compatibility_group.add_sql_instruction(
      "INSERT INTO insert_table VALUES (4, 'd', 40, '2020-01-01');", expected=ResultString.SUCCESS)

  atomicity_group = insert_test.add_execution_group('statement atomicity', [compatibility_group])
  atomicity_group.add_sql_instruction(
      "INSERT INTO insert_table VALUES "
      "(5, 'e', 50, '2020-01-01'), ('bad', 'f', 60, '2020-01-02'), (7, 'g', 70, '2020-01-03');",
      expected=ResultString.FAILURE)
  atomicity_group.add_sql_instruction(
      "INSERT INTO insert_table VALUES (8, 'h', 80, '2020-01-01'), (9, 'i', 90);",
      expected=ResultString.FAILURE)
  atomicity_group.add_sql_instruction(
      "INSERT INTO insert_table VALUES "
      "(10, 'j', 100, '2020-01-01'), (11, 'k', 110, '2021-02-29');",
      expected=ResultString.FAILURE)
  atomicity_group.add_sort_block_sql_instruction('''
      SELECT * FROM insert_table;
      id | name | score | birthday
      1 | a | 10 | 2024-02-29
      2 | b | 20 | 2025-01-02
      3 | c | 30 | 2000-02-29
      4 | d | 40 | 2020-01-01
  ''')

  persistence_group = insert_test.add_execution_group('restart persistence', [atomicity_group])
  persistence_group.add_instruction(RestartInstruction())
  persistence_group.add_sort_block_sql_instruction('''
      SELECT * FROM insert_table;
      id | name | score | birthday
      1 | a | 10 | 2024-02-29
      2 | b | 20 | 2025-01-02
      3 | c | 30 | 2000-02-29
      4 | d | 40 | 2020-01-01
  ''')

  return insert_test
