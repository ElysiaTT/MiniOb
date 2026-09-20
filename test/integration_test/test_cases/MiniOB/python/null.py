import __init__

from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  null_test = TestCase()
  null_test.name = 'null'

  init_group = null_test.add_execution_group('nullable schema and indexes')
  init_group.add_sql_instruction(
      'CREATE TABLE null_table(id int NOT NULL, num int NULL, price float NOT NULL, birthday date NULL);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction('CREATE INDEX null_num ON null_table(num);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE UNIQUE INDEX null_birthday ON null_table(birthday);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO null_table VALUES "
      "(1, 18, 10.0, '2020-01-01'), (2, NULL, 20.0, '2010-01-11'), "
      "(3, 12, 30.0, NULL), (4, 15, 40.0, '2021-01-31'), (5, NULL, 50.0, NULL);",
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO null_table VALUES (NULL, 1, 1.0, '2022-01-01');", expected=ResultString.FAILURE)
  init_group.add_sql_instruction(
      "INSERT INTO null_table VALUES (6, 1, NULL, '2022-01-01');", expected=ResultString.FAILURE)

  query_group = null_test.add_execution_group('null predicates and comparisons', [init_group])
  query_group.add_block_sql_instruction('''
      SELECT id, num, birthday FROM null_table ORDER BY id;
      id | num | birthday
      1 | 18 | 2020-01-01
      2 | NULL | 2010-01-11
      3 | 12 | NULL
      4 | 15 | 2021-01-31
      5 | NULL | NULL
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE num IS NULL ORDER BY id;
      id
      2
      5
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE num = 12 ORDER BY id;
      id
      3
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE birthday = '2020-01-01' ORDER BY id;
      id
      1
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE birthday IS NOT NULL ORDER BY id;
      id
      1
      2
      4
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE num = NULL ORDER BY id;
      id
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE NULL = NULL ORDER BY id;
      id
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE NULL IS NULL ORDER BY id;
      id
      1
      2
      3
      4
      5
  ''')
  query_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE 1 IS NOT NULL ORDER BY id;
      id
      1
      2
      3
      4
      5
  ''')

  aggregate_group = null_test.add_execution_group('aggregates ignore null', [query_group])
  aggregate_group.add_block_sql_instruction('''
      SELECT count(*), count(num), count(birthday), avg(num) FROM null_table;
      count(*) | count(num) | count(birthday) | avg(num)
      5 | 3 | 3 | 15
  ''')
  aggregate_group.add_sql_instruction('CREATE TABLE all_null(id int, num int NULL);', expected=ResultString.SUCCESS)
  aggregate_group.add_sql_instruction('INSERT INTO all_null VALUES (1, NULL), (2, NULL);', expected=ResultString.SUCCESS)
  aggregate_group.add_block_sql_instruction('''
      SELECT count(num), min(num), max(num), avg(num) FROM all_null;
      count(num) | min(num) | max(num) | avg(num)
      0 | NULL | NULL | NULL
  ''')

  mutation_group = null_test.add_execution_group('update delete and multi-table behavior', [aggregate_group])
  mutation_group.add_sql_instruction('UPDATE null_table SET num = NULL WHERE id = 1;', expected=ResultString.SUCCESS)
  mutation_group.add_sql_instruction('UPDATE null_table SET price = NULL WHERE id = 1;', expected=ResultString.FAILURE)
  mutation_group.add_sql_instruction('CREATE TABLE null_other(id int, num int NULL);', expected=ResultString.SUCCESS)
  mutation_group.add_sql_instruction('INSERT INTO null_other VALUES (1, 12), (2, NULL);', expected=ResultString.SUCCESS)
  mutation_group.add_block_sql_instruction('''
      SELECT null_table.id, null_other.id FROM null_table, null_other WHERE null_table.num = null_other.num ORDER BY null_table.id;
      null_table.id | null_other.id
      3 | 1
  ''')
  mutation_group.add_sql_instruction('DELETE FROM null_table WHERE birthday IS NULL;', expected=ResultString.SUCCESS)
  mutation_group.add_block_sql_instruction('''
      SELECT id FROM null_table ORDER BY id;
      id
      1
      2
      4
  ''')

  persistence_group = null_test.add_execution_group('nullable metadata persistence', [mutation_group])
  persistence_group.add_instruction(RestartInstruction())
  persistence_group.add_sql_instruction('INSERT INTO null_table VALUES (6, NULL, 60.0, NULL);', expected=ResultString.SUCCESS)
  persistence_group.add_block_sql_instruction('''
      SELECT id FROM null_table WHERE num IS NULL ORDER BY id;
      id
      1
      2
      6
  ''')

  return null_test
