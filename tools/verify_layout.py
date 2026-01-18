import re
import os

def parse_lyt(filepath):
    """Parses a .lyt file into a list of dictionaries representing elements."""
    elements = []
    current_element = {}
    
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';'): continue
            
            if line.startswith('[') and line.endswith(']'):
                if current_element:
                    elements.append(current_element)
                current_element = {'section': line[1:-1]}
            elif '=' in line:
                key, val = line.split('=', 1)
                current_element[key.strip().lower()] = val.strip()
                
    if current_element:
        elements.append(current_element)
    return elements

def analyze_layout(elements):
    """Analyzes layout elements for overlaps and potential issues."""
    print(f"{'Section':<25} {'Type':<10} {'ID':<6} {'X':<5} {'Y':<5} {'Details'}")
    print("-" * 80)
    
    for el in elements:
        # Focus on bottom area elements (y > 500)
        try:
            y = int(el.get('y', -1))
        except ValueError:
            y = -1 # Handle 'center' or other non-integers
            
        if y > 500 or (y == -1 and el.get('y') == 'center'):
             details = []
             if 'font' in el: details.append(f"font={el['font']}")
             if 'animation' in el: details.append(f"ani={el['animation']}")
             
             print(f"{el['section']:<25} {el.get('type','?'):<10} {el.get('id','?'):<6} {el.get('x','?'):<5} {el.get('y','?'):<5} {', '.join(details)}")

def compare_fonts(mapselec_path, scenario_path):
    """Compares font usage between mapselec.lyt and scenario.lyt."""
    map_els = parse_lyt(mapselec_path)
    scen_els = parse_lyt(scenario_path)
    
    map_fonts = {el['section']: el['font'] for el in map_els if 'font' in el}
    scen_fonts = {el['section']: el['font'] for el in scen_els if 'font' in el}
    
    print("\n=== Font Comparison ===")
    print("Element                         MapSelec Font    Scenario Font    Match?")
    print("-" * 80)
    
    # Compare similar elements
    common_elements = [
        ('Starting Cash', 'Difficulty Label', ''), # Heuristic matching
        ('Map list Label', 'Scenario List Label', '14101'), 
        ('Back to main menu', 'Back to main menu', '50009')
    ]
    
    # Check specific IDs known to be working in Scenario
    working_fonts = set(scen_fonts.values())
    print(f"Working fonts in Scenario menu: {working_fonts}")
    
    for section, font in map_fonts.items():
        status = "OK" if font in working_fonts else "WARNING: Unknown Font"
        print(f"{section:<30} {font:<15} {status}")

if __name__ == "__main__":
    base_path = "ui"
    mapselec = os.path.join(base_path, "mapselec.lyt")
    scenario = os.path.join(base_path, "scenario.lyt")
    
    if os.path.exists(mapselec) and os.path.exists(scenario):
        print("Analyzing mapselec.lyt...")
        elements = parse_lyt(mapselec)
        analyze_layout(elements)
        compare_fonts(mapselec, scenario)
    else:
        print(f"Error: Could not find files in {base_path}")
