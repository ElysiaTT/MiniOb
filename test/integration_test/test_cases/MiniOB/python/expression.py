import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  expression_test = TestCase()
  expression_test.name = 'expression'

  init_group = expression_test.add_execution_group('init data')
  init_group.add_sql_instruction(
      'CREATE TABLE expression_table(id int, a int, b float, name char(6));',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO expression_table VALUES "
      "(1, 10, 5, 'one'), (2, 2, 10, 'two'), "
      "(3, 8, 9, 'three'), (4, 4, 1.5, 'four');",
      expected=ResultString.SUCCESS)

  projection_group = expression_test.add_execution_group('projection precedence and unary', [init_group])
  projection_group.add_block_sql_instruction('''
      SELECT id, a + 2 * 3, (a + 2) * 3, -a, b / 2 FROM expression_table ORDER BY id;
      id | a + 2 * 3 | (a + 2) * 3 | -a | b / 2
      1 | 16 | 36 | -10 | 2.5
      2 | 8 | 12 | -2 | 5
      3 | 14 | 30 | -8 | 4.5
      4 | 10 | 18 | -4 | 0.75
  ''')

  predicate_group = expression_test.add_execution_group('arithmetic predicates', [init_group])
  predicate_group.add_block_sql_instruction('''
      SELECT id FROM expression_table WHERE a + 10 > b * 2 + 3 - (a + 10) / 3 ORDER BY id;
      id
      1
      3
      4
  ''')
  predicate_group.add_block_sql_instruction('''
      SELECT id FROM expression_table WHERE -a < -5 AND a / 2 >= b - 5 ORDER BY id;
      id
      1
      3
  ''')
  predicate_group.add_block_sql_instruction('''
      SELECT id FROM expression_table WHERE 1 + 2 * 3 = 7 ORDER BY id;
      id
      1
      2
      3
      4
  ''')

  join_group = expression_test.add_execution_group('multi-table expressions', [init_group])
  join_group.add_sql_instruction('CREATE TABLE expression_other(x int, y float);', expected=ResultString.SUCCESS)
  join_group.add_sql_instruction(
      'INSERT INTO expression_other VALUES (1, 4), (2, 8), (3, 2);', expected=ResultString.SUCCESS)
  join_group.add_block_sql_instruction('''
      SELECT expression_table.id, expression_other.x FROM expression_table, expression_other WHERE expression_table.id = expression_other.x AND expression_table.a + expression_other.x > expression_table.b ORDER BY expression_table.id;
      id | x
      1 | 1
      3 | 3
  ''')

  mutation_group = expression_test.add_execution_group('update and delete predicates', [init_group])
  mutation_group.add_sql_instruction(
      'UPDATE expression_table SET a = 100 WHERE a * 2 = 16;', expected=ResultString.SUCCESS)
  mutation_group.add_sql_instruction(
      'DELETE FROM expression_table WHERE a + id = 8;', expected=ResultString.SUCCESS)
  mutation_group.add_block_sql_instruction('''
      SELECT id, a FROM expression_table ORDER BY id;
      id | a
      1 | 10
      2 | 2
      3 | 100
  ''')

  validation_group = expression_test.add_execution_group('invalid operands and fields', [init_group])
  validation_group.add_sql_instruction(
      'SELECT name + 1 FROM expression_table;', expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT id FROM expression_table WHERE missing + 1 = 2;', expected=ResultString.FAILURE)

  return expression_test
