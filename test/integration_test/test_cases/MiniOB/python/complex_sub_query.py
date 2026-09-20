import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  subquery_test = TestCase()
  subquery_test.name = 'complex-sub-query'

  init_group = subquery_test.add_execution_group('init complex subquery data')
  init_group.add_sql_instruction(
      "SET execution_mode = 'chunk_iterator';", expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE csq_outer(id int, grp int, score int);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE csq_inner(owner_id int, score int, tag char(2));', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE csq_tags(tag char(2), enabled int);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'INSERT INTO csq_outer VALUES (1, 1, 5), (2, 1, 8), (3, 2, 3), (4, 3, 10), (5, 4, 1);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO csq_inner VALUES (1, 5, 'a'), (1, 7, 'b'), (2, 8, 'a'), (2, 9, 'c'), "
      "(3, 1, 'a'), (3, 4, 'b'), (4, 10, 'c'), (6, 1, 'a');",
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO csq_tags VALUES ('a', 1), ('b', 0), ('c', 1);",
      expected=ResultString.SUCCESS)

  nested_group = subquery_test.add_execution_group('nested and multi-table subqueries', [init_group])
  nested_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT score FROM csq_inner WHERE tag IN (SELECT tag FROM csq_tags WHERE enabled = 1)) ORDER BY id;
      id
      1
      2
      4
      5
  ''')
  nested_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT csq_inner.score FROM csq_inner, csq_tags WHERE csq_inner.tag = csq_tags.tag AND csq_tags.enabled = 1 AND csq_inner.owner_id > 1) ORDER BY id;
      id
      2
      4
      5
  ''')

  correlated_group = subquery_test.add_execution_group('correlated subqueries', [init_group])
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT score FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) ORDER BY id;
      id
      1
      2
      4
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score NOT IN (SELECT score FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) ORDER BY id;
      id
      3
      5
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score < (SELECT max(score) FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) ORDER BY id;
      id
      1
      2
      3
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE (SELECT max(score) FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) = score ORDER BY id;
      id
      4
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score = (SELECT min(score) FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id AND tag >= 'b') ORDER BY id;
      id
      4
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT csq_inner.score FROM csq_inner, csq_tags WHERE csq_inner.tag = csq_tags.tag AND csq_tags.enabled = 1 AND csq_inner.owner_id = csq_outer.id) ORDER BY id;
      id
      1
      2
      4
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT score FROM csq_inner WHERE owner_id = grp) ORDER BY id;
      id
      1
  ''')
  correlated_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT score FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) AND grp < (SELECT count(*) FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) ORDER BY id;
      id
      1
      2
  ''')

  ancestor_group = subquery_test.add_execution_group('nested ancestor correlation', [init_group])
  ancestor_group.add_block_sql_instruction('''
      SELECT id FROM csq_outer WHERE score IN (SELECT score FROM csq_inner WHERE tag IN (SELECT tag FROM csq_tags WHERE enabled = 1 AND csq_inner.owner_id = csq_outer.id)) ORDER BY id;
      id
      1
      2
      4
  ''')

  validation_group = subquery_test.add_execution_group('correlated scalar validation', [init_group])
  validation_group.add_sql_instruction(
      'SELECT id FROM csq_outer WHERE score = '
      '(SELECT score FROM csq_inner WHERE csq_inner.owner_id = csq_outer.id) ORDER BY id;',
      expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT id FROM csq_outer WHERE score IN '
      '(SELECT score FROM csq_inner WHERE csq_inner.owner_id = missing_outer.id);',
      expected=ResultString.FAILURE)

  return subquery_test
