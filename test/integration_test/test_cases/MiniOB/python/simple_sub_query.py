import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  subquery_test = TestCase()
  subquery_test.name = 'simple-sub-query'

  init_group = subquery_test.add_execution_group('init data')
  init_group.add_sql_instruction(
      "SET execution_mode = 'chunk_iterator';", expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE ssq_outer(id int, score int, feat float);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE ssq_inner(id int, score int, feat float);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE ssq_empty(id int, score int, feat float);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'INSERT INTO ssq_outer VALUES (1, 4, 11.2), (2, 2, 12), (3, 3, 13.5), (4, 8, 9);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'INSERT INTO ssq_inner VALUES (1, 2, 13), (2, 7, 10.5), (5, 3, 12.6), (1, 4, 20);',
      expected=ResultString.SUCCESS)

  membership_group = subquery_test.add_execution_group('in and not in', [init_group])
  membership_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE id IN (SELECT id FROM ssq_inner) ORDER BY id;
      id
      1
      2
  ''')
  membership_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE score NOT IN (SELECT score FROM ssq_inner) ORDER BY id;
      id
      4
  ''')
  membership_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE id IN (SELECT id FROM ssq_inner WHERE score > 3) ORDER BY id;
      id
      1
      2
  ''')

  scalar_group = subquery_test.add_execution_group('scalar aggregate comparisons', [init_group])
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE score = (SELECT avg(score) FROM ssq_inner) ORDER BY id;
      id
      1
  ''')
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE (SELECT avg(score) FROM ssq_inner) = score ORDER BY id;
      id
      1
  ''')
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE feat >= (SELECT min(feat) FROM ssq_inner) ORDER BY id;
      id
      1
      2
      3
  ''')
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE (SELECT max(feat) FROM ssq_inner) > feat AND score > 2 ORDER BY id;
      id
      1
      3
      4
  ''')
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE id < (SELECT count(*) FROM ssq_inner) ORDER BY id;
      id
      1
      2
      3
  ''')
  scalar_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE score = (SELECT score FROM ssq_inner WHERE id = 5) ORDER BY id;
      id
      3
  ''')

  type_group = subquery_test.add_execution_group('string and date values', [init_group])
  type_group.add_sql_instruction(
      'CREATE TABLE ssq_types(code char(4), day date);', expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      'CREATE TABLE ssq_types_inner(code char(4), day date);', expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      "INSERT INTO ssq_types VALUES ('a', '2024-01-01'), ('b', '2024-01-02'), ('d', '2024-01-04');",
      expected=ResultString.SUCCESS)
  type_group.add_sql_instruction(
      "INSERT INTO ssq_types_inner VALUES ('b', '2024-01-02'), ('c', '2024-01-03');",
      expected=ResultString.SUCCESS)
  type_group.add_block_sql_instruction('''
      SELECT code FROM ssq_types WHERE code IN (SELECT code FROM ssq_types_inner) ORDER BY code;
      code
      b
  ''')
  type_group.add_block_sql_instruction('''
      SELECT code FROM ssq_types WHERE day < (SELECT max(day) FROM ssq_types_inner) ORDER BY code;
      code
      a
      b
  ''')

  empty_group = subquery_test.add_execution_group('empty subquery results', [init_group])
  empty_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE id IN (SELECT id FROM ssq_empty) ORDER BY id;
      id
  ''')
  empty_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE id NOT IN (SELECT id FROM ssq_empty) ORDER BY id;
      id
      1
      2
      3
      4
  ''')
  empty_group.add_block_sql_instruction('''
      SELECT id FROM ssq_outer WHERE feat < (SELECT max(feat) FROM ssq_empty) ORDER BY id;
      id
  ''')
  empty_group.add_block_sql_instruction('''
      SELECT id FROM ssq_empty WHERE id NOT IN (SELECT id FROM ssq_inner) ORDER BY id;
      id
  ''')

  validation_group = subquery_test.add_execution_group('invalid scalar and column counts', [type_group])
  validation_group.add_sql_instruction(
      'SELECT id FROM ssq_outer WHERE score = (SELECT score FROM ssq_inner);',
      expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT id FROM ssq_outer WHERE id = (SELECT id, score FROM ssq_inner);',
      expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT id FROM ssq_outer WHERE id IN (SELECT id, score FROM ssq_inner);',
      expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT id FROM ssq_outer WHERE id IN (SELECT code FROM ssq_types_inner);',
      expected=ResultString.FAILURE)

  return subquery_test
